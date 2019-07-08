/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "jit/jit_memory_region.h"

#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/unique_fd.h>
#include <gtest/gtest.h>

#include "base/globals.h"
#include "base/memfd.h"
#include "base/utils.h"
#include "common_runtime_test.h"

namespace art {
namespace jit {

// These tests only run on bionic.
#if defined(__BIONIC__)
static constexpr int kReturnFromFault = 42;

// These globals are only set in child processes.
void* gAddrToFaultOn = nullptr;

void handler(int ATTRIBUTE_UNUSED, siginfo_t* info, void* ATTRIBUTE_UNUSED) {
  CHECK_EQ(info->si_addr, gAddrToFaultOn);
  exit(kReturnFromFault);
}

static void registerSignalHandler() {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = handler;
  sigaction(SIGSEGV, &sa, nullptr);
}

class TestZygoteMemory : public testing::Test {
 public:
  void BasicTest() {
    // Zygote JIT memory only works on kernels that don't segfault on flush.
    TEST_DISABLED_FOR_KERNELS_WITH_CACHE_SEGFAULT();
    std::string error_msg;
    size_t size = kPageSize;
    android::base::unique_fd fd(JitMemoryRegion::CreateZygoteMemory(size, &error_msg));
    CHECK_NE(fd.get(), -1);

    // Create a writable mapping.
    int32_t* addr = reinterpret_cast<int32_t*>(
        mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0));
    CHECK(addr != nullptr);
    CHECK_NE(addr, MAP_FAILED);

    // Test that we can write into the mapping.
    addr[0] = 42;
    CHECK_EQ(addr[0], 42);

    // Protect the memory.
    bool res = JitMemoryRegion::ProtectZygoteMemory(fd.get(), &error_msg);
    CHECK(res);

    // Test that we can still write into the mapping.
    addr[0] = 2;
    CHECK_EQ(addr[0], 2);

    // Test that we cannot create another writable mapping.
    int32_t* addr2 = reinterpret_cast<int32_t*>(
        mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0));
    CHECK_EQ(addr2, MAP_FAILED);

    // With the existing mapping, we can toggle read/write.
    CHECK_EQ(mprotect(addr, size, PROT_READ), 0) << strerror(errno);
    CHECK_EQ(mprotect(addr, size, PROT_READ | PROT_WRITE), 0) << strerror(errno);

    // Test mremap with old_size = 0. From the man pages:
    //    If the value of old_size is zero, and old_address refers to a shareable mapping
    //    (see mmap(2) MAP_SHARED), then mremap() will create a new mapping of the same pages.
    addr2 = reinterpret_cast<int32_t*>(mremap(addr, 0, kPageSize, MREMAP_MAYMOVE));
    CHECK_NE(addr2, MAP_FAILED);

    // Test that we can  write into the remapped mapping.
    addr2[0] = 3;
    CHECK_EQ(addr2[0], 3);

    addr2 = reinterpret_cast<int32_t*>(mremap(addr, kPageSize, 2 * kPageSize, MREMAP_MAYMOVE));
    CHECK_NE(addr2, MAP_FAILED);

    // Test that we can  write into the remapped mapping.
    addr2[0] = 4;
    CHECK_EQ(addr2[0], 4);
  }

  void TestUnmapWritableAfterFork() {
    // Zygote JIT memory only works on kernels that don't segfault on flush.
    TEST_DISABLED_FOR_KERNELS_WITH_CACHE_SEGFAULT();
    std::string error_msg;
    size_t size = kPageSize;
    int32_t* addr = nullptr;
    int32_t* addr2 = nullptr;
    {
      android::base::unique_fd fd(JitMemoryRegion::CreateZygoteMemory(size, &error_msg));
      CHECK_NE(fd.get(), -1);

      // Create a writable mapping.
      addr = reinterpret_cast<int32_t*>(
          mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0));
      CHECK(addr != nullptr);
      CHECK_NE(addr, MAP_FAILED);

      // Test that we can write into the mapping.
      addr[0] = 42;
      CHECK_EQ(addr[0], 42);

      // Create a read-only mapping.
      addr2 = reinterpret_cast<int32_t*>(
          mmap(nullptr, kPageSize, PROT_READ, MAP_SHARED, fd.get(), 0));
      CHECK(addr2 != nullptr);

      // Protect the memory.
      bool res = JitMemoryRegion::ProtectZygoteMemory(fd.get(), &error_msg);
      CHECK(res);
    }
    // At this point, the fd has been dropped, but the memory mappings are still
    // there.

    // Create a mapping of atomic ints to communicate between processes.
    android::base::unique_fd fd2(JitMemoryRegion::CreateZygoteMemory(size, &error_msg));
    CHECK_NE(fd2.get(), -1);
    std::atomic<int32_t>* shared = reinterpret_cast<std::atomic<int32_t>*>(
        mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd2.get(), 0));

    // Values used for the tests below.
    const int32_t parent_value = 66;
    const int32_t child_value = 33;
    const int32_t starting_value = 22;

    shared[0] = 0;
    addr[0] = starting_value;
    CHECK_EQ(addr[0], starting_value);
    CHECK_EQ(addr2[0], starting_value);
    pid_t pid = fork();
    if (pid == 0) {
      // Test that we can write into the mapping.
      addr[0] = child_value;
      CHECK_EQ(addr[0], child_value);
      CHECK_EQ(addr2[0], child_value);

      // Unmap the writable mappping.
      munmap(addr, kPageSize);

      CHECK_EQ(addr2[0], child_value);

      // Notify parent process.
      shared[0] = 1;

      // Wait for parent process for a new value.
      while (shared[0] != 2) {
        sched_yield();
      }
      CHECK_EQ(addr2[0], parent_value);

      // Test that we cannot write into the mapping. The signal handler will
      // exit the process.
      gAddrToFaultOn = addr;
      registerSignalHandler();
      // This write will trigger a fault, as `addr` is unmapped.
      addr[0] = child_value + 1;
      exit(0);
    } else {
      while (shared[0] != 1) {
        sched_yield();
      }
      CHECK_EQ(addr[0], child_value);
      CHECK_EQ(addr2[0], child_value);
      addr[0] = parent_value;
      // Notify the child if the new value.
      shared[0] = 2;
      int status;
      CHECK_EQ(waitpid(pid, &status, 0), pid);
      CHECK(WIFEXITED(status)) << strerror(errno);
      CHECK_EQ(WEXITSTATUS(status), kReturnFromFault);
      CHECK_EQ(addr[0], parent_value);
      CHECK_EQ(addr2[0], parent_value);
      munmap(addr, kPageSize);
      munmap(addr2, kPageSize);
      munmap(shared, kPageSize);
    }
  }

  void TestMadviseDontFork() {
    // Zygote JIT memory only works on kernels that don't segfault on flush.
    TEST_DISABLED_FOR_KERNELS_WITH_CACHE_SEGFAULT();
    std::string error_msg;
    size_t size = kPageSize;
    int32_t* addr = nullptr;
    int32_t* addr2 = nullptr;
    {
      android::base::unique_fd fd(JitMemoryRegion::CreateZygoteMemory(size, &error_msg));
      CHECK_NE(fd.get(), -1);

      // Create a writable mapping.
      addr = reinterpret_cast<int32_t*>(
          mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0));
      CHECK(addr != nullptr);
      CHECK_NE(addr, MAP_FAILED);
      CHECK_EQ(madvise(addr, kPageSize, MADV_DONTFORK), 0);

      // Test that we can write into the mapping.
      addr[0] = 42;
      CHECK_EQ(addr[0], 42);

      // Create a read-only mapping.
      addr2 = reinterpret_cast<int32_t*>(
          mmap(nullptr, kPageSize, PROT_READ, MAP_SHARED, fd.get(), 0));
      CHECK(addr2 != nullptr);

      // Protect the memory.
      bool res = JitMemoryRegion::ProtectZygoteMemory(fd.get(), &error_msg);
      CHECK(res);
    }
    // At this point, the fd has been dropped, but the memory mappings are still
    // there.

    // Create a mapping of atomic ints to communicate between processes.
    android::base::unique_fd fd2(JitMemoryRegion::CreateZygoteMemory(size, &error_msg));
    CHECK_NE(fd2.get(), -1);
    std::atomic<int32_t>* shared = reinterpret_cast<std::atomic<int32_t>*>(
        mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd2.get(), 0));

    // Values used for the tests below.
    const int32_t parent_value = 66;
    const int32_t child_value = 33;
    const int32_t starting_value = 22;

    shared[0] = 0;
    addr[0] = starting_value;
    CHECK_EQ(addr[0], starting_value);
    CHECK_EQ(addr2[0], starting_value);
    pid_t pid = fork();
    if (pid == 0) {
      CHECK_EQ(addr2[0], starting_value);

      // Notify parent process.
      shared[0] = 1;

      // Wait for parent process for new value.
      while (shared[0] != 2) {
        sched_yield();
      }

      CHECK_EQ(addr2[0], parent_value);
      // Test that we cannot write into the mapping. The signal handler will
      // exit the process.
      gAddrToFaultOn = addr;
      registerSignalHandler();
      addr[0] = child_value + 1;
      exit(0);
    } else {
      while (shared[0] != 1) {
        sched_yield();
      }
      CHECK_EQ(addr[0], starting_value);
      CHECK_EQ(addr2[0], starting_value);
      addr[0] = parent_value;
      // Notify the child of the new value.
      shared[0] = 2;
      int status;
      CHECK_EQ(waitpid(pid, &status, 0), pid);
      CHECK(WIFEXITED(status)) << strerror(errno);
      CHECK_EQ(WEXITSTATUS(status), kReturnFromFault);
      CHECK_EQ(addr[0], parent_value);
      CHECK_EQ(addr2[0], parent_value);

      munmap(addr, kPageSize);
      munmap(addr2, kPageSize);
      munmap(shared, kPageSize);
    }
  }

  // This code is testing some behavior that ART could potentially use: get a
  // copy-on-write mapping that can incorporate changes from a shared mapping
  // owned by another process.
  void TestFromSharedToPrivate() {
    // Zygote JIT memory only works on kernels that don't segfault on flush.
    TEST_DISABLED_FOR_KERNELS_WITH_CACHE_SEGFAULT();
    // This test is only for memfd with future write sealing support:
    // 1) ashmem with PROT_READ doesn't permit mapping MAP_PRIVATE | PROT_WRITE
    // 2) ashmem mapped MAP_PRIVATE discards the contents already written.
    if (!art::IsSealFutureWriteSupported()) {
      return;
    }
    std::string error_msg;
    size_t size = kPageSize;
    int32_t* addr = nullptr;
    android::base::unique_fd fd(JitMemoryRegion::CreateZygoteMemory(size, &error_msg));
    CHECK_NE(fd.get(), -1);

    // Create a writable mapping.
    addr = reinterpret_cast<int32_t*>(
        mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0));
    CHECK(addr != nullptr);
    CHECK_NE(addr, MAP_FAILED);

    // Test that we can write into the mapping.
    addr[0] = 42;
    CHECK_EQ(addr[0], 42);

    // Create another mapping of atomic ints to communicate between processes.
    android::base::unique_fd fd2(JitMemoryRegion::CreateZygoteMemory(size, &error_msg));
    CHECK_NE(fd2.get(), -1);
    std::atomic<int32_t>* shared = reinterpret_cast<std::atomic<int32_t>*>(
        mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd2.get(), 0));

    // Protect the memory.
    CHECK(JitMemoryRegion::ProtectZygoteMemory(fd.get(), &error_msg));

    // Values used for the tests below.
    const int32_t parent_value = 66;
    const int32_t child_value = 33;
    const int32_t starting_value = 22;

    // Check that updates done by a child mapping write-private are not visible
    // to the parent.
    addr[0] = starting_value;
    shared[0] = 0;
    pid_t pid = fork();
    if (pid == 0) {
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd.get(), 0),
               addr);
      addr[0] = child_value;
      exit(0);
    } else {
      int status;
      CHECK_EQ(waitpid(pid, &status, 0), pid);
      CHECK(WIFEXITED(status)) << strerror(errno);
      CHECK_EQ(addr[0], starting_value);
    }

    addr[0] = starting_value;
    shared[0] = 0;

    // Check getting back and forth on shared mapping.
    pid = fork();
    if (pid == 0) {
      // Map it private with write access. MAP_FIXED will replace the existing
      // mapping.
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd.get(), 0),
               addr);
      addr[0] = child_value;
      CHECK_EQ(addr[0], child_value);

      // Check that mapping shared with write access fails.
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd.get(), 0),
               MAP_FAILED);
      CHECK_EQ(errno, EPERM);

      // Map shared with read access.
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ, MAP_SHARED | MAP_FIXED, fd.get(), 0), addr);
      CHECK_NE(addr[0], child_value);

      // Wait for the parent to notify.
      while (shared[0] != 1) {
        sched_yield();
      }
      CHECK_EQ(addr[0], parent_value);

      // Notify the parent for getting a new update of the buffer.
      shared[0] = 2;

      // Map it private again.
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd.get(), 0),
               addr);
      addr[0] = child_value + 1;
      CHECK_EQ(addr[0], child_value + 1);

      // And map it back shared.
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ, MAP_SHARED | MAP_FIXED, fd.get(), 0), addr);
      while (shared[0] != 3) {
        sched_yield();
      }
      CHECK_EQ(addr[0], parent_value + 1);
      exit(0);
    } else {
      addr[0] = parent_value;
      CHECK_EQ(addr[0], parent_value);

      // Notify the child of the new value.
      shared[0] = 1;

      // Wait for the child to ask for a new value;
      while (shared[0] != 2) {
        sched_yield();
      }
      addr[0] = parent_value + 1;
      CHECK_EQ(addr[0], parent_value + 1);

      // Notify the child of a new value.
      shared[0] = 3;
      int status;
      CHECK_EQ(waitpid(pid, &status, 0), pid);
      CHECK(WIFEXITED(status)) << strerror(errno);
      CHECK_EQ(addr[0], parent_value + 1);
    }

    // Check that updates done by the parent are visible after a new mmap
    // write-private.
    shared[0] = 0;
    addr[0] = starting_value;
    pid = fork();
    if (pid == 0) {
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd.get(), 0),
               addr);
      CHECK_EQ(addr[0], starting_value);
      addr[0] = child_value;
      CHECK_EQ(addr[0], child_value);

      // Notify the parent to update the buffer.
      shared[0] = 1;

      // Wait for the parent update.
      while (shared[0] != 2) {
        sched_yield();
      }
      // Test the buffer still contains our own data, and not the parent's.
      CHECK_EQ(addr[0], child_value);

      // Test the buffer contains the parent data after a new mmap.
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd.get(), 0),
               addr);
      CHECK_EQ(addr[0], parent_value);
      exit(0);
    } else {
      // Wait for the child to start
      while (shared[0] != 1) {
        sched_yield();
      }
      CHECK_EQ(addr[0], starting_value);
      addr[0] = parent_value;
      // Notify the child that the buffer has been written.
      shared[0] = 2;
      int status;
      CHECK_EQ(waitpid(pid, &status, 0), pid);
      CHECK(WIFEXITED(status)) << strerror(errno);
      CHECK_EQ(addr[0], parent_value);
    }

    // Check that updates done by the parent are visible for a new mmap
    // write-private that hasn't written to the buffer yet.
    shared[0] = 0;
    addr[0] = starting_value;
    pid = fork();
    if (pid == 0) {
      CHECK_EQ(mmap(addr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd.get(), 0),
               addr);
      CHECK_EQ(addr[0], starting_value);
      // Notify the parent for a new update of the buffer.
      shared[0] = 1;
      while (addr[0] != parent_value) {
        sched_yield();
      }
      addr[0] = child_value;
      CHECK_EQ(addr[0], child_value);
      exit(0);
    } else {
      while (shared[0] != 1) {
        sched_yield();
      }
      CHECK_EQ(addr[0], starting_value);
      addr[0] = parent_value;
      int status;
      CHECK_EQ(waitpid(pid, &status, 0), pid);
      CHECK(WIFEXITED(status)) << strerror(errno);
      CHECK_EQ(addr[0], parent_value);
    }
    munmap(addr, kPageSize);
    munmap(shared, kPageSize);
  }
};

TEST_F(TestZygoteMemory, BasicTest) {
  BasicTest();
}

TEST_F(TestZygoteMemory, TestUnmapWritableAfterFork) {
  TestUnmapWritableAfterFork();
}

TEST_F(TestZygoteMemory, TestMadviseDontFork) {
  TestMadviseDontFork();
}

TEST_F(TestZygoteMemory, TestFromSharedToPrivate) {
  TestFromSharedToPrivate();
}

#endif  // defined (__BIONIC__)

}  // namespace jit
}  // namespace art
