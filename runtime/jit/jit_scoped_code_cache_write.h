/*
 * Copyright 2019 The Android Open Source Project
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

#ifndef ART_RUNTIME_JIT_JIT_SCOPED_CODE_CACHE_WRITE_H_
#define ART_RUNTIME_JIT_JIT_SCOPED_CODE_CACHE_WRITE_H_

#include <sys/mman.h>

#include "base/systrace.h"
#include "base/utils.h"  // For CheckedCall

namespace art {
namespace jit {

class JitMemoryRegion;

static constexpr int kProtR = PROT_READ;
static constexpr int kProtRW = PROT_READ | PROT_WRITE;
static constexpr int kProtRWX = PROT_READ | PROT_WRITE | PROT_EXEC;
static constexpr int kProtRX = PROT_READ | PROT_EXEC;

// Helper for toggling JIT memory R <-> RW.
class ScopedCodeCacheWrite : ScopedTrace {
 public:
  explicit ScopedCodeCacheWrite(const JitMemoryRegion& region)
      : ScopedTrace("ScopedCodeCacheWrite"),
        region_(region) {
    ScopedTrace trace("mprotect all");
    const MemMap* const updatable_pages = region.GetUpdatableCodeMapping();
    if (updatable_pages != nullptr) {
      int prot = region.HasDualCodeMapping() ? kProtRW : kProtRWX;
      CheckedCall(mprotect, "Cache +W", updatable_pages->Begin(), updatable_pages->Size(), prot);
    }
  }

  ~ScopedCodeCacheWrite() {
    ScopedTrace trace("mprotect code");
    const MemMap* const updatable_pages = region_.GetUpdatableCodeMapping();
    if (updatable_pages != nullptr) {
      int prot = region_.HasDualCodeMapping() ? kProtR : kProtRX;
      CheckedCall(mprotect, "Cache -W", updatable_pages->Begin(), updatable_pages->Size(), prot);
    }
  }

 private:
  const JitMemoryRegion& region_;

  DISALLOW_COPY_AND_ASSIGN(ScopedCodeCacheWrite);
};

}  // namespace jit
}  // namespace art

#endif  // ART_RUNTIME_JIT_JIT_SCOPED_CODE_CACHE_WRITE_H_
