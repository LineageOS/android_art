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

#include "executable-inl.h"

#include "art_method-inl.h"
#include "object-inl.h"

namespace art {
namespace mirror {

template <PointerSize kPointerSize>
void Executable::InitializeFromArtMethod(ArtMethod* method) {
  // We're initializing a newly allocated object, so we do not need to record that under
  // a transaction. If the transaction is aborted, the whole object shall be unreachable.
  auto* interface_method = method->GetInterfaceMethodIfProxy(kPointerSize);
  SetArtMethod</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(method);
  SetFieldObject</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      DeclaringClassOffset(), method->GetDeclaringClass());
  SetFieldObject</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      DeclaringClassOfOverriddenMethodOffset(), interface_method->GetDeclaringClass());
  SetField32</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      AccessFlagsOffset(), method->GetAccessFlags());
  SetField32</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      DexMethodIndexOffset(), method->GetDexMethodIndex());
}

template void Executable::InitializeFromArtMethod<PointerSize::k32>(ArtMethod* method);
template void Executable::InitializeFromArtMethod<PointerSize::k64>(ArtMethod* method);

}  // namespace mirror
}  // namespace art
