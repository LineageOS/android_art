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

#ifndef ART_RUNTIME_NTERP_HELPERS_H_
#define ART_RUNTIME_NTERP_HELPERS_H_

#include "quick/quick_method_frame_info.h"

namespace art {

class ArtMethod;

/**
 * The frame size nterp will use for the given method.
 */
size_t NterpGetFrameSize(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_);

/**
 * Returns the QuickMethodFrameInfo of the given frame corresponding to the
 * given method.
 */
QuickMethodFrameInfo NterpFrameInfo(ArtMethod** frame)
    REQUIRES_SHARED(Locks::mutator_lock_);

/**
 * Returns the dex PC at which the given nterp frame is executing.
 */
uint32_t NterpGetDexPC(ArtMethod** frame)
    REQUIRES_SHARED(Locks::mutator_lock_);

/**
 * Returns the reference array to be used by the GC to visit references in an
 * nterp frame.
 */
uintptr_t NterpGetReferenceArray(ArtMethod** frame)
      REQUIRES_SHARED(Locks::mutator_lock_);

/**
 * Returns the dex register array to be used by the GC to update references in
 * an nterp frame.
 */
uintptr_t NterpGetRegistersArray(ArtMethod** frame)
      REQUIRES_SHARED(Locks::mutator_lock_);

/**
 * Returns the nterp landing pad for catching an exception.
 */
uintptr_t NterpGetCatchHandler();

/**
 * Returns the value of dex register number `vreg` in the given frame.
 */
uint32_t NterpGetVReg(ArtMethod** frame, uint16_t vreg)
    REQUIRES_SHARED(Locks::mutator_lock_);

/**
 * Returns the value of dex register number `vreg` in the given frame if it is a
 * reference. Return 0 otehrwise.
 */
uint32_t NterpGetVRegReference(ArtMethod** frame, uint16_t vreg)
    REQUIRES_SHARED(Locks::mutator_lock_);

}  // namespace art

#endif  // ART_RUNTIME_NTERP_HELPERS_H_
