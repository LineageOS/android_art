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

#include "handle.h"

#include "mirror/object.h"
#include "mirror/accessible_object.h"
#include "mirror/array.h"
#include "mirror/call_site.h"
#include "mirror/class.h"
#include "mirror/class_ext.h"
#include "mirror/dex_cache.h"
#include "mirror/emulated_stack_frame.h"
#include "mirror/executable.h"
#include "mirror/field.h"
#include "mirror/iftable.h"
#include "mirror/method_handle_impl.h"
#include "mirror/method_handles_lookup.h"
#include "mirror/method_type.h"
#include "mirror/method.h"
#include "mirror/proxy.h"
#include "mirror/reference.h"
#include "mirror/stack_trace_element.h"
#include "mirror/string.h"
#include "mirror/throwable.h"
#include "mirror/var_handle.h"

#include "class_root-inl.h"

namespace art {

#define MAKE_OBJECT_FOR_GDB(ROOT, NAME, MIRROR)                 \
  template <> MIRROR* Handle<MIRROR>::GetFromGdb() {            \
    return Get();                                               \
  }                                                             \
  template <> mirror::Object* Handle<MIRROR>::ObjectFromGdb() { \
    return static_cast<mirror::Object*>(Get());                 \
  }

CLASS_MIRROR_ROOT_LIST(MAKE_OBJECT_FOR_GDB)

#undef MAKE_OBJECT_FOR_GDB

}  // namespace art
