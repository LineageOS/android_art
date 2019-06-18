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

#include <android-base/unique_fd.h>
#include <gtest/gtest.h>
#include <sys/mman.h>

#include "base/globals.h"

namespace art {
namespace jit {

class TestZygoteMemory : public testing::Test {
 public:
  void BasicTest() {
#if defined(__BIONIC__)
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
#endif
  }
};

TEST_F(TestZygoteMemory, BasicTest) {
  BasicTest();
}

}  // namespace jit
}  // namespace art
