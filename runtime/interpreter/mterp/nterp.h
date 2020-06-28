/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef ART_RUNTIME_INTERPRETER_MTERP_NTERP_H_
#define ART_RUNTIME_INTERPRETER_MTERP_NTERP_H_

#include "base/globals.h"

extern "C" void* artNterpAsmInstructionStart[];
extern "C" void* artNterpAsmInstructionEnd[];

namespace art {

class ArtMethod;

namespace interpreter {

void CheckNterpAsmConstants();
bool IsNterpSupported();
bool CanRuntimeUseNterp();
bool CanMethodUseNterp(ArtMethod* method);
const void* GetNterpEntryPoint();

// The hotness threshold where we trigger JIT compilation or OSR.
constexpr uint16_t kNterpHotnessMask = 0xffff;

// The maximum we allow an nterp frame to be.
constexpr size_t kNterpMaxFrame = 3 * KB;

// The maximum size for each nterp opcode handler.
constexpr size_t kNterpHandlerSize = 128;

}  // namespace interpreter
}  // namespace art

#endif  // ART_RUNTIME_INTERPRETER_MTERP_NTERP_H_
