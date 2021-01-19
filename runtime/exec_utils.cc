/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "exec_utils.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <string>
#include <vector>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "runtime.h"

namespace art {

using android::base::StringPrintf;

namespace {

std::string ToCommandLine(const std::vector<std::string>& args) {
  return android::base::Join(args, ' ');
}

// Fork and execute a command specified in a subprocess.
// If there is a runtime (Runtime::Current != nullptr) then the subprocess is created with the
// same environment that existed when the runtime was started.
// Returns the process id of the child process on success, -1 otherwise.
pid_t ExecWithoutWait(std::vector<std::string>& arg_vector) {
  // Convert the args to char pointers.
  const char* program = arg_vector[0].c_str();
  std::vector<char*> args;
  for (const auto& arg : arg_vector) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  // fork and exec
  pid_t pid = fork();
  if (pid == 0) {
    // no allocation allowed between fork and exec

    // change process groups, so we don't get reaped by ProcessManager
    setpgid(0, 0);

    // (b/30160149): protect subprocesses from modifications to LD_LIBRARY_PATH, etc.
    // Use the snapshot of the environment from the time the runtime was created.
    char** envp = (Runtime::Current() == nullptr) ? nullptr : Runtime::Current()->GetEnvSnapshot();
    if (envp == nullptr) {
      execv(program, &args[0]);
    } else {
      execve(program, &args[0], envp);
    }
    PLOG(ERROR) << "Failed to execve(" << ToCommandLine(arg_vector) << ")";
    // _exit to avoid atexit handlers in child.
    _exit(1);
  } else {
    return pid;
  }
}

}  // namespace

int ExecAndReturnCode(std::vector<std::string>& arg_vector, std::string* error_msg) {
  pid_t pid = ExecWithoutWait(arg_vector);
  if (pid == -1) {
    *error_msg = StringPrintf("Failed to execv(%s) because fork failed: %s",
                              ToCommandLine(arg_vector).c_str(), strerror(errno));
    return -1;
  }

  // wait for subprocess to finish
  int status = -1;
  pid_t got_pid = TEMP_FAILURE_RETRY(waitpid(pid, &status, 0));
  if (got_pid != pid) {
    *error_msg = StringPrintf("Failed after fork for execv(%s) because waitpid failed: "
                              "wanted %d, got %d: %s",
                              ToCommandLine(arg_vector).c_str(), pid, got_pid, strerror(errno));
    return -1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
}

int ExecAndReturnCode(std::vector<std::string>& arg_vector,
                      time_t timeout_secs,
                      bool* timed_out,
                      std::string* error_msg) {
  *timed_out = false;

  // Start subprocess.
  pid_t pid = ExecWithoutWait(arg_vector);
  if (pid == -1) {
    *error_msg = StringPrintf("Failed to execv(%s) because fork failed: %s",
                              ToCommandLine(arg_vector).c_str(), strerror(errno));
    return -1;
  }

  // Add SIGCHLD to the signal set.
  sigset_t child_mask, original_mask;
  sigemptyset(&child_mask);
  sigaddset(&child_mask, SIGCHLD);
  if (sigprocmask(SIG_BLOCK, &child_mask, &original_mask) == -1) {
    *error_msg = StringPrintf("Failed to set sigprocmask(): %s", strerror(errno));
    return -1;
  }

  // Wait for a SIGCHLD notification.
  errno = 0;
  timespec ts = {timeout_secs, 0};
  int wait_result = TEMP_FAILURE_RETRY(sigtimedwait(&child_mask, nullptr, &ts));
  int wait_errno = errno;

  // Restore the original signal set.
  if (sigprocmask(SIG_SETMASK, &original_mask, nullptr) == -1) {
    *error_msg = StringPrintf("Fail to restore sigprocmask(): %s", strerror(errno));
    if (wait_result == 0) {
      return -1;
    }
  }

  // Having restored the signal set, see if we need to terminate the subprocess.
  if (wait_result == -1) {
    if (wait_errno == EAGAIN) {
      *error_msg = "Timed out.";
      *timed_out = true;
    } else {
      *error_msg = StringPrintf("Failed to sigtimedwait(): %s", strerror(errno));
    }
    if (kill(pid, SIGKILL) != 0) {
      PLOG(ERROR) << "Failed to kill() subprocess: ";
    }
  }

  // Wait for subprocess to finish.
  int status = -1;
  pid_t got_pid = TEMP_FAILURE_RETRY(waitpid(pid, &status, 0));
  if (got_pid != pid) {
    *error_msg = StringPrintf("Failed after fork for execv(%s) because waitpid failed: "
                              "wanted %d, got %d: %s",
                              ToCommandLine(arg_vector).c_str(), pid, got_pid, strerror(errno));
    return -1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
}


bool Exec(std::vector<std::string>& arg_vector, std::string* error_msg) {
  int status = ExecAndReturnCode(arg_vector, error_msg);
  if (status != 0) {
    *error_msg = StringPrintf("Failed execv(%s) because non-0 exit status",
                              ToCommandLine(arg_vector).c_str());
    return false;
  }
  return true;
}

}  // namespace art
