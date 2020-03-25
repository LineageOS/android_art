/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ART_RUNTIME_JIT_DEBUGGER_INTERFACE_H_
#define ART_RUNTIME_JIT_DEBUGGER_INTERFACE_H_

#include <functional>
#include <inttypes.h>
#include <vector>

#include "arch/instruction_set_features.h"
#include "base/array_ref.h"
#include "base/locks.h"

namespace art {

class DexFile;
class Mutex;
class Thread;
struct JITCodeEntry;

// Must be called before zygote forks.
// Used to ensure that zygote's mini-debug-info can be shared with apps.
void NativeDebugInfoPreFork();

// Must be called after zygote forks.
void NativeDebugInfoPostFork();

ArrayRef<const uint8_t> GetJITCodeEntrySymFile(const JITCodeEntry*);

// Notify native tools (e.g. libunwind) that DEX file has been opened.
void AddNativeDebugInfoForDex(Thread* self, const DexFile* dexfile);

// Notify native tools (e.g. libunwind) that DEX file has been closed.
void RemoveNativeDebugInfoForDex(Thread* self, const DexFile* dexfile);

// Notify native tools (e.g. libunwind) that JIT has compiled a single new method.
// The method will make copy of the passed ELF file (to shrink it to the minimum size).
// If packing is allowed, the ELF file might be merged with others to save space
// (however, this drops all ELF sections other than symbols names and unwinding info).
void AddNativeDebugInfoForJit(const void* code_ptr,
                              const std::vector<uint8_t>& symfile,
                              bool allow_packing)
    REQUIRES_SHARED(Locks::jit_lock_);  // Might need JIT code cache to allocate memory.

// Notify native tools (e.g. libunwind) that JIT code has been garbage collected.
// The actual removal might be lazy. Removal of address that was not added is no-op.
void RemoveNativeDebugInfoForJit(const void* code_ptr);

// Merge and compress entries to save space.
void RepackNativeDebugInfoForJit()
    REQUIRES_SHARED(Locks::jit_lock_);  // Might need JIT code cache to allocate memory.

// Returns approximate memory used by debug info for JIT code.
size_t GetJitMiniDebugInfoMemUsage() REQUIRES_SHARED(Locks::jit_lock_);

// Get the lock which protects the native debug info.
// Used only in tests to unwind while the JIT thread is running.
// TODO: Unwinding should be race-free. Remove this.
Mutex* GetNativeDebugInfoLock();

// Call given callback for every stored symbol. The callback parameters are (address, size, name).
void ForEachNativeDebugSymbol(std::function<void(const void*, size_t, const char*)> cb);

}  // namespace art

#endif  // ART_RUNTIME_JIT_DEBUGGER_INTERFACE_H_
