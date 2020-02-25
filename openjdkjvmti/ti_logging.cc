/* Copyright (C) 2018 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "ti_logging.h"

#include "art_jvmti.h"

#include "base/logging.h"
#include "base/mutex.h"
#include "base/strlcpy.h"
#include "cmdline_types.h"
#include "jvmti.h"
#include "thread-current-inl.h"

namespace openjdkjvmti {

jvmtiError LogUtil::GetLastError(jvmtiEnv* env, char** data) {
  if (env == nullptr || data == nullptr) {
    return ERR(INVALID_ENVIRONMENT);
  }
  ArtJvmTiEnv* tienv = ArtJvmTiEnv::AsArtJvmTiEnv(env);
  art::MutexLock mu(art::Thread::Current(), tienv->last_error_mutex_);
  if (tienv->last_error_.empty()) {
    return ERR(ABSENT_INFORMATION);
  }
  const size_t size = tienv->last_error_.size() + 1;
  char* out;
  jvmtiError err = tienv->Allocate(size, reinterpret_cast<unsigned char**>(&out));
  if (err != OK) {
    return err;
  }
  strlcpy(out, tienv->last_error_.c_str(), size);
  *data = out;
  return OK;
}

jvmtiError LogUtil::ClearLastError(jvmtiEnv* env) {
  if (env == nullptr) {
    return ERR(INVALID_ENVIRONMENT);
  }
  ArtJvmTiEnv* tienv = ArtJvmTiEnv::AsArtJvmTiEnv(env);
  art::MutexLock mu(art::Thread::Current(), tienv->last_error_mutex_);
  tienv->last_error_.clear();
  return OK;
}

jvmtiError LogUtil::SetVerboseFlagExt(jvmtiEnv* env, const char* data, jboolean enable) {
  if (env == nullptr) {
    return ERR(INVALID_ENVIRONMENT);
  } else if (data == nullptr) {
    return ERR(NULL_POINTER);
  }
  bool new_value = (enable == JNI_TRUE) ? true : false;
  art::CmdlineType<art::LogVerbosity> cmdline_parser;
  std::string parse_data(data);
  art::CmdlineType<art::LogVerbosity>::Result result = cmdline_parser.Parse(parse_data);
  if (result.IsError()) {
    JVMTI_LOG(INFO, env) << "Invalid verbose argument: '" << parse_data << "'. Error was "
                         << result.GetMessage();
    return ERR(ILLEGAL_ARGUMENT);
  }

  const art::LogVerbosity& input_verbosity = result.GetValue();
  const bool* verbosity_arr = reinterpret_cast<const bool*>(&input_verbosity);
  bool* g_log_verbosity_arr = reinterpret_cast<bool*>(&art::gLogVerbosity);
  // Copy/invert the verbosity byte-by-byte (sizeof(bool) == 1).
  for (size_t i = 0; i < sizeof(art::LogVerbosity); i++) {
    if (verbosity_arr[i]) {
      g_log_verbosity_arr[i] = new_value;
    }
  }
  return OK;
}

jvmtiError LogUtil::SetVerboseFlag(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                   jvmtiVerboseFlag flag,
                                   jboolean value) {
  if (flag == jvmtiVerboseFlag::JVMTI_VERBOSE_OTHER) {
    // OTHER is special, as it's 0, so can't do a bit check.
    bool val = (value == JNI_TRUE) ? true : false;

    art::gLogVerbosity.collector = val;
    art::gLogVerbosity.compiler = val;
    art::gLogVerbosity.deopt = val;
    art::gLogVerbosity.heap = val;
    art::gLogVerbosity.interpreter = val;
    art::gLogVerbosity.jdwp = val;
    art::gLogVerbosity.jit = val;
    art::gLogVerbosity.monitor = val;
    art::gLogVerbosity.oat = val;
    art::gLogVerbosity.profiler = val;
    art::gLogVerbosity.signals = val;
    art::gLogVerbosity.simulator = val;
    art::gLogVerbosity.startup = val;
    art::gLogVerbosity.third_party_jni = val;
    art::gLogVerbosity.threads = val;
    art::gLogVerbosity.verifier = val;
    // Do not set verifier-debug.
    art::gLogVerbosity.image = val;
    art::gLogVerbosity.plugin = val;

    // Note: can't switch systrace_lock_logging. That requires changing entrypoints.

    art::gLogVerbosity.agents = val;
  } else {
    // Spec isn't clear whether "flag" is a mask or supposed to be single. We implement the mask
    // semantics.
    constexpr std::underlying_type<jvmtiVerboseFlag>::type kMask =
        jvmtiVerboseFlag::JVMTI_VERBOSE_GC |
        jvmtiVerboseFlag::JVMTI_VERBOSE_CLASS |
        jvmtiVerboseFlag::JVMTI_VERBOSE_JNI;
    if ((flag & ~kMask) != 0) {
      return ERR(ILLEGAL_ARGUMENT);
    }

    bool val = (value == JNI_TRUE) ? true : false;

    if ((flag & jvmtiVerboseFlag::JVMTI_VERBOSE_GC) != 0) {
      art::gLogVerbosity.gc = val;
    }

    if ((flag & jvmtiVerboseFlag::JVMTI_VERBOSE_CLASS) != 0) {
      art::gLogVerbosity.class_linker = val;
    }

    if ((flag & jvmtiVerboseFlag::JVMTI_VERBOSE_JNI) != 0) {
      art::gLogVerbosity.jni = val;
    }
  }

  return ERR(NONE);
}

}  // namespace openjdkjvmti
