/* Copyright (C) 2017 The Android Open Source Project
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
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <vector>

#include "jvmti.h"
#include "ti_extension.h"

#include "art_jvmti.h"
#include "events.h"
#include "jni_id_type.h"
#include "runtime-inl.h"
#include "ti_allocator.h"
#include "ti_class.h"
#include "ti_ddms.h"
#include "ti_dump.h"
#include "ti_heap.h"
#include "ti_logging.h"
#include "ti_monitor.h"
#include "ti_redefine.h"
#include "ti_search.h"
#include "transform.h"

#include "thread-inl.h"

namespace openjdkjvmti {

struct CParamInfo {
  const char* name;
  jvmtiParamKind kind;
  jvmtiParamTypes base_type;
  jboolean null_ok;

  jvmtiParamInfo ToParamInfo(jvmtiEnv* env,
                             /*out*/std::vector<JvmtiUniquePtr<char[]>>* char_buffers,
                             /*out*/jvmtiError* err) const {
    JvmtiUniquePtr<char[]> param_name = CopyString(env, name, err);
    char* name_ptr = param_name.get();
    char_buffers->push_back(std::move(param_name));
    return jvmtiParamInfo{ name_ptr, kind, base_type, null_ok };
  }
};

jvmtiError ExtensionUtil::GetExtensionFunctions(jvmtiEnv* env,
                                                jint* extension_count_ptr,
                                                jvmtiExtensionFunctionInfo** extensions) {
  if (extension_count_ptr == nullptr || extensions == nullptr) {
    return ERR(NULL_POINTER);
  }

  std::vector<jvmtiExtensionFunctionInfo> ext_vector;

  // Holders for allocated values.
  std::vector<JvmtiUniquePtr<char[]>> char_buffers;
  std::vector<JvmtiUniquePtr<jvmtiParamInfo[]>> param_buffers;
  std::vector<JvmtiUniquePtr<jvmtiError[]>> error_buffers;

  auto add_extension = [&](jvmtiExtensionFunction func,
                           const char* id,
                           const char* short_description,
                           const std::vector<CParamInfo>& params,
                           const std::vector<jvmtiError>& errors) {
    jvmtiExtensionFunctionInfo func_info;
    jvmtiError error;

    func_info.func = func;

    JvmtiUniquePtr<char[]> id_ptr = CopyString(env, id, &error);
    if (id_ptr == nullptr) {
      return error;
    }
    func_info.id = id_ptr.get();
    char_buffers.push_back(std::move(id_ptr));

    JvmtiUniquePtr<char[]> descr = CopyString(env, short_description, &error);
    if (descr == nullptr) {
      return error;
    }
    func_info.short_description = descr.get();
    char_buffers.push_back(std::move(descr));

    func_info.param_count = params.size();
    if (!params.empty()) {
      JvmtiUniquePtr<jvmtiParamInfo[]> params_ptr =
          AllocJvmtiUniquePtr<jvmtiParamInfo[]>(env, params.size(), &error);
      if (params_ptr == nullptr) {
        return error;
      }
      func_info.params = params_ptr.get();
      param_buffers.push_back(std::move(params_ptr));

      for (jint i = 0; i != func_info.param_count; ++i) {
        func_info.params[i] = params[i].ToParamInfo(env, &char_buffers, &error);
        if (error != OK) {
          return error;
        }
      }
    } else {
      func_info.params = nullptr;
    }

    func_info.error_count = errors.size();
    if (!errors.empty()) {
      JvmtiUniquePtr<jvmtiError[]> errors_ptr =
          AllocJvmtiUniquePtr<jvmtiError[]>(env, errors.size(), &error);
      if (errors_ptr == nullptr) {
        return error;
      }
      func_info.errors = errors_ptr.get();
      error_buffers.push_back(std::move(errors_ptr));

      for (jint i = 0; i != func_info.error_count; ++i) {
        func_info.errors[i] = errors[i];
      }
    } else {
      func_info.errors = nullptr;
    }

    ext_vector.push_back(func_info);

    return ERR(NONE);
  };

  jvmtiError error;

  // Heap extensions.
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(HeapExtensions::GetObjectHeapId),
      "com.android.art.heap.get_object_heap_id",
      "Retrieve the heap id of the the object tagged with the given argument. An "
          "arbitrary object is chosen if multiple objects exist with the same tag.",
      {
          { "tag", JVMTI_KIND_IN, JVMTI_TYPE_JLONG, false},
          { "heap_id", JVMTI_KIND_OUT, JVMTI_TYPE_JINT, false}
      },
      { JVMTI_ERROR_NOT_FOUND });
  if (error != ERR(NONE)) {
    return error;
  }

  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(HeapExtensions::GetHeapName),
      "com.android.art.heap.get_heap_name",
      "Retrieve the name of the heap with the given id.",
      {
          { "heap_id", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false},
          { "heap_name", JVMTI_KIND_ALLOC_BUF, JVMTI_TYPE_CCHAR, false}
      },
      { JVMTI_ERROR_ILLEGAL_ARGUMENT });
  if (error != ERR(NONE)) {
    return error;
  }

  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(HeapExtensions::IterateThroughHeapExt),
      "com.android.art.heap.iterate_through_heap_ext",
      "Iterate through a heap. This is equivalent to the standard IterateThroughHeap function,"
      " except for additionally passing the heap id of the current object. The jvmtiHeapCallbacks"
      " structure is reused, with the callbacks field overloaded to a signature of "
      "jint (*)(jlong, jlong, jlong*, jint length, void*, jint).",
      {
          { "heap_filter", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false},
          { "klass", JVMTI_KIND_IN, JVMTI_TYPE_JCLASS, true},
          { "callbacks", JVMTI_KIND_IN_PTR, JVMTI_TYPE_CVOID, false},
          { "user_data", JVMTI_KIND_IN_PTR, JVMTI_TYPE_CVOID, true}
      },
      {
          ERR(MUST_POSSESS_CAPABILITY),
          ERR(INVALID_CLASS),
          ERR(NULL_POINTER),
      });
  if (error != ERR(NONE)) {
    return error;
  }

  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(AllocUtil::GetGlobalJvmtiAllocationState),
      "com.android.art.alloc.get_global_jvmti_allocation_state",
      "Returns the total amount of memory currently allocated by all jvmtiEnvs through the"
      " 'Allocate' jvmti function. This does not include any memory that has been deallocated"
      " through the 'Deallocate' function. This number is approximate and might not correspond"
      " exactly to the sum of the sizes of all not freed allocations.",
      {
          { "currently_allocated", JVMTI_KIND_OUT, JVMTI_TYPE_JLONG, false},
      },
      { ERR(NULL_POINTER) });
  if (error != ERR(NONE)) {
    return error;
  }

  // DDMS extension
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(DDMSUtil::HandleChunk),
      "com.android.art.internal.ddm.process_chunk",
      "Handles a single ddms chunk request and returns a response. The reply data is in the ddms"
      " chunk format. It returns the processed chunk. This is provided for backwards compatibility"
      " reasons only. Agents should avoid making use of this extension when possible and instead"
      " use the other JVMTI entrypoints explicitly.",
      {
        { "type_in", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
        { "length_in", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
        { "data_in", JVMTI_KIND_IN_BUF, JVMTI_TYPE_JBYTE, true },
        { "type_out", JVMTI_KIND_OUT, JVMTI_TYPE_JINT, false },
        { "data_len_out", JVMTI_KIND_OUT, JVMTI_TYPE_JINT, false },
        { "data_out", JVMTI_KIND_ALLOC_BUF, JVMTI_TYPE_JBYTE, false }
      },
      { ERR(NULL_POINTER), ERR(ILLEGAL_ARGUMENT), ERR(OUT_OF_MEMORY) });
  if (error != ERR(NONE)) {
    return error;
  }

  // GetClassLoaderClassDescriptors extension
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(ClassUtil::GetClassLoaderClassDescriptors),
      "com.android.art.class.get_class_loader_class_descriptors",
      "Retrieves a list of all the classes (as class descriptors) that the given class loader is"
      " capable of being the defining class loader for. The return format is a list of"
      " null-terminated descriptor strings of the form \"L/java/lang/Object;\". Each descriptor"
      " will be in the list at most once. If the class_loader is null the bootclassloader will be"
      " used. If the class_loader is not null it must either be a java.lang.BootClassLoader, a"
      " dalvik.system.BaseDexClassLoader or a derived type. The data_out list and all elements"
      " must be deallocated by the caller.",
      {
        { "class_loader", JVMTI_KIND_IN, JVMTI_TYPE_JOBJECT, true },
        { "class_descriptor_count_out", JVMTI_KIND_OUT, JVMTI_TYPE_JINT, false },
        { "data_out", JVMTI_KIND_ALLOC_ALLOC_BUF, JVMTI_TYPE_CCHAR, false },
      },
      {
        ERR(NULL_POINTER),
        ERR(ILLEGAL_ARGUMENT),
        ERR(OUT_OF_MEMORY),
        ERR(NOT_IMPLEMENTED),
      });
  if (error != ERR(NONE)) {
    return error;
  }

  // Raw monitors no suspend
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(MonitorUtil::RawMonitorEnterNoSuspend),
      "com.android.art.concurrent.raw_monitor_enter_no_suspend",
      "Normally entering a monitor will not return until both the monitor is locked and the"
      " current thread is not suspended. This method will return once the monitor is locked"
      " even if the thread is suspended. Note that using rawMonitorWait will wait until the"
      " thread is not suspended again on wakeup and so should be avoided.",
      {
          { "raw_monitor", JVMTI_KIND_IN_PTR, JVMTI_TYPE_CVOID, false },
      },
      {
        ERR(NULL_POINTER),
        ERR(INVALID_MONITOR),
      });
  if (error != ERR(NONE)) {
    return error;
  }

  // GetLastError extension
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(LogUtil::GetLastError),
      "com.android.art.misc.get_last_error_message",
      "In some cases the jvmti plugin will log data about errors to the android logcat. These can"
      " be useful to tools so we make (some) of the messages available here as well. This will"
      " fill the given 'msg' buffer with the last non-fatal message associated with this"
      " jvmti-env. Note this is best-effort only, not all log messages will be accessible through"
      " this API. This will return the last error-message from all threads. Care should be taken"
      " interpreting the return value when used with a multi-threaded program. The error message"
      " will only be cleared by a call to 'com.android.art.misc.clear_last_error_message' and will"
      " not be cleared by intervening successful calls. If no (tracked) error message has been"
      " sent since the last call to clear_last_error_message this API will return"
      " JVMTI_ERROR_ABSENT_INFORMATION. Not all failures will cause an error message to be"
      " recorded.",
      {
          { "msg", JVMTI_KIND_ALLOC_BUF, JVMTI_TYPE_CCHAR, false },
      },
      {
        ERR(NULL_POINTER),
        ERR(ABSENT_INFORMATION),
      });
  if (error != ERR(NONE)) {
    return error;
  }

  // ClearLastError extension
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(LogUtil::ClearLastError),
      "com.android.art.misc.clear_last_error_message",
      "Clears the error message returned by 'com.android.art.misc.get_last_error_message'.",
      { },
      { });
  if (error != ERR(NONE)) {
    return error;
  }

  // DumpInternalState
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(DumpUtil::DumpInternalState),
      "com.android.art.misc.get_plugin_internal_state",
      "Gets internal state about the plugin and serializes it to the given msg. "
      "There is no particular format to this message beyond being human readable.",
      {
          { "msg", JVMTI_KIND_ALLOC_BUF, JVMTI_TYPE_CCHAR, false },
      },
      { ERR(NULL_POINTER) });
  if (error != ERR(NONE)) {
    return error;
  }

  // AddToDexClassLoader
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(SearchUtil::AddToDexClassLoader),
      "com.android.art.classloader.add_to_dex_class_loader",
      "Adds a dexfile to a given dalvik.system.BaseDexClassLoader in a manner similar to"
      " AddToSystemClassLoader.",
      {
        { "classloader", JVMTI_KIND_IN, JVMTI_TYPE_JOBJECT, false },
        { "segment", JVMTI_KIND_IN_PTR, JVMTI_TYPE_CCHAR, false },
      },
      {
         ERR(NULL_POINTER),
         ERR(CLASS_LOADER_UNSUPPORTED),
         ERR(ILLEGAL_ARGUMENT),
         ERR(WRONG_PHASE),
      });
  if (error != ERR(NONE)) {
    return error;
  }

  // AddToDexClassLoaderInMemory
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(SearchUtil::AddToDexClassLoaderInMemory),
      "com.android.art.classloader.add_to_dex_class_loader_in_memory",
      "Adds a dexfile buffer to a given dalvik.system.BaseDexClassLoader in a manner similar to"
      " AddToSystemClassLoader. This may only be done during the LIVE phase. The buffer is copied"
      " and the caller is responsible for deallocating it after this call.",
      {
        { "classloader", JVMTI_KIND_IN, JVMTI_TYPE_JOBJECT, false },
        { "dex_bytes", JVMTI_KIND_IN_BUF, JVMTI_TYPE_CCHAR, false },
        { "dex_bytes_len", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
      },
      {
         ERR(NULL_POINTER),
         ERR(CLASS_LOADER_UNSUPPORTED),
         ERR(ILLEGAL_ARGUMENT),
         ERR(WRONG_PHASE),
      });
  if (error != ERR(NONE)) {
    return error;
  }

  // ChangeArraySize
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(HeapExtensions::ChangeArraySize),
      "com.android.art.heap.change_array_size",
      "Changes the size of a java array. As far as all JNI and java code is concerned this is"
      " atomic. Must have can_tag_objects capability. If the new length of the array is smaller"
      " than the original length, then the array will be truncated to the new length. Otherwise,"
      " all new slots will be filled with null, 0, or False as appropriate for the array type.",
      {
        { "array", JVMTI_KIND_IN, JVMTI_TYPE_JOBJECT, false },
        { "new_size", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
      },
      {
         ERR(NULL_POINTER),
         ERR(MUST_POSSESS_CAPABILITY),
         ERR(ILLEGAL_ARGUMENT),
         ERR(OUT_OF_MEMORY),
      });
  if (error != ERR(NONE)) {
    return error;
  }

  // These require index-ids and debuggable to function
  art::Runtime* runtime = art::Runtime::Current();
  if (runtime->GetJniIdType() == art::JniIdType::kIndices &&
      (runtime->GetInstrumentation()->IsForcedInterpretOnly() || runtime->IsJavaDebuggable())) {
    // IsStructurallyModifiableClass
    error = add_extension(
        reinterpret_cast<jvmtiExtensionFunction>(Redefiner::IsStructurallyModifiableClass),
        "com.android.art.class.is_structurally_modifiable_class",
        "Returns whether a class can potentially be 'structurally' redefined using the various"
        " structural redefinition extensions provided.",
        {
          { "klass", JVMTI_KIND_IN, JVMTI_TYPE_JCLASS, false },
          { "result", JVMTI_KIND_OUT, JVMTI_TYPE_JBOOLEAN, false },
        },
        {
          ERR(INVALID_CLASS),
          ERR(NULL_POINTER),
        });
    if (error != ERR(NONE)) {
      return error;
    }

    // StructurallyRedefineClasses
    error = add_extension(
        reinterpret_cast<jvmtiExtensionFunction>(Redefiner::StructurallyRedefineClasses),
        "com.android.art.class.structurally_redefine_classes",
        "Entrypoint for structural class redefinition. Has the same signature as RedefineClasses."
        " Only supports additive changes, methods and fields may not be removed. Supertypes and"
        " implemented interfaces may not be changed. After calling this"
        " com.android.art.structural_dex_file_load_hook events will be triggered, followed by"
        " re-transformable ClassFileLoadHook events. After this method completes subsequent"
        " RetransformClasses calls will use the input to this function as the initial class"
        " definition.",
        {
            { "num_classes", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
            { "class_definitions", JVMTI_KIND_IN_BUF, JVMTI_TYPE_CVOID, false },
        },
        {
            ERR(CLASS_LOADER_UNSUPPORTED),
            ERR(FAILS_VERIFICATION),
            ERR(ILLEGAL_ARGUMENT),
            ERR(INVALID_CLASS),
            ERR(MUST_POSSESS_CAPABILITY),
            ERR(MUST_POSSESS_CAPABILITY),
            ERR(NULL_POINTER),
            ERR(OUT_OF_MEMORY),
            ERR(UNMODIFIABLE_CLASS),
            ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED),
            ERR(UNSUPPORTED_REDEFINITION_METHOD_ADDED),
            ERR(UNSUPPORTED_REDEFINITION_METHOD_DELETED),
            ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
        });
    if (error != ERR(NONE)) {
      return error;
    }

    // StructurallyRedefineClassDirect
    error = add_extension(
        reinterpret_cast<jvmtiExtensionFunction>(Redefiner::StructurallyRedefineClassDirect),
        "com.android.art.UNSAFE.class.structurally_redefine_class_direct",
        "Temporary prototype entrypoint for redefining a single class structurally. Currently this"
        " only supports adding new static fields to a class without any instances."
        " ClassFileLoadHook events will NOT be triggered. This does not currently support creating"
        " obsolete methods. This function only has rudimentary error checking. This should not be"
        " used except for testing.",
        {
          { "klass", JVMTI_KIND_IN, JVMTI_TYPE_JCLASS, false },
          { "new_def", JVMTI_KIND_IN_BUF, JVMTI_TYPE_CCHAR, false },
          { "new_def_len", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
        },
        {
          ERR(CLASS_LOADER_UNSUPPORTED),
          ERR(FAILS_VERIFICATION),
          ERR(ILLEGAL_ARGUMENT),
          ERR(INVALID_CLASS),
          ERR(MUST_POSSESS_CAPABILITY),
          ERR(MUST_POSSESS_CAPABILITY),
          ERR(NULL_POINTER),
          ERR(OUT_OF_MEMORY),
          ERR(UNMODIFIABLE_CLASS),
          ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED),
          ERR(UNSUPPORTED_REDEFINITION_METHOD_ADDED),
          ERR(UNSUPPORTED_REDEFINITION_METHOD_DELETED),
          ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
        });
    if (error != ERR(NONE)) {
      return error;
    }
  } else {
    LOG(INFO) << "debuggable & jni-type indices are required to implement structural "
              << "class redefinition extensions.";
  }
  // SetVerboseFlagExt
  error = add_extension(
      reinterpret_cast<jvmtiExtensionFunction>(LogUtil::SetVerboseFlagExt),
      "com.android.art.misc.set_verbose_flag_ext",
      "Sets the verbose flags selected by the 'option' c-string. Valid options are anything that"
      " would be accepted by the -verbose:<option> runtime flag. The verbose selections are turned"
      " on if 'enable' is set to true and disabled otherwise. You may select multiple options at"
      " once using commas just like with the -verbose:<option> flag. For example \"class,deopt,gc\""
      " is equivalent to turning on all of the VLOG(class_linker), VLOG(deopt) and VLOG(gc)"
      " messages.",
      {
        { "option", JVMTI_KIND_IN_BUF, JVMTI_TYPE_CCHAR, false },
        { "enable", JVMTI_KIND_IN, JVMTI_TYPE_JBOOLEAN, false },
      },
      {
         ERR(NULL_POINTER),
         ERR(ILLEGAL_ARGUMENT),
      });
  if (error != ERR(NONE)) {
    return error;
  }

  // Copy into output buffer.

  *extension_count_ptr = ext_vector.size();
  JvmtiUniquePtr<jvmtiExtensionFunctionInfo[]> out_data =
      AllocJvmtiUniquePtr<jvmtiExtensionFunctionInfo[]>(env, ext_vector.size(), &error);
  if (out_data == nullptr) {
    return error;
  }
  memcpy(out_data.get(),
          ext_vector.data(),
          ext_vector.size() * sizeof(jvmtiExtensionFunctionInfo));
  *extensions = out_data.release();

  // Release all the buffer holders, we're OK now.
  for (auto& holder : char_buffers) {
    holder.release();
  }
  for (auto& holder : param_buffers) {
    holder.release();
  }
  for (auto& holder : error_buffers) {
    holder.release();
  }

  return OK;
}


jvmtiError ExtensionUtil::GetExtensionEvents(jvmtiEnv* env,
                                             jint* extension_count_ptr,
                                             jvmtiExtensionEventInfo** extensions) {
  std::vector<jvmtiExtensionEventInfo> ext_vector;

  // Holders for allocated values.
  std::vector<JvmtiUniquePtr<char[]>> char_buffers;
  std::vector<JvmtiUniquePtr<jvmtiParamInfo[]>> param_buffers;

  auto add_extension = [&](ArtJvmtiEvent extension_event_index,
                           const char* id,
                           const char* short_description,
                           const std::vector<CParamInfo>& params) {
    DCHECK(IsExtensionEvent(extension_event_index)) << static_cast<jint>(extension_event_index);
    jvmtiExtensionEventInfo event_info;
    jvmtiError error;

    event_info.extension_event_index = static_cast<jint>(extension_event_index);

    JvmtiUniquePtr<char[]> id_ptr = CopyString(env, id, &error);
    if (id_ptr == nullptr) {
      return error;
    }
    event_info.id = id_ptr.get();
    char_buffers.push_back(std::move(id_ptr));

    JvmtiUniquePtr<char[]> descr = CopyString(env, short_description, &error);
    if (descr == nullptr) {
      return error;
    }
    event_info.short_description = descr.get();
    char_buffers.push_back(std::move(descr));

    event_info.param_count = params.size();
    if (!params.empty()) {
      JvmtiUniquePtr<jvmtiParamInfo[]> params_ptr =
          AllocJvmtiUniquePtr<jvmtiParamInfo[]>(env, params.size(), &error);
      if (params_ptr == nullptr) {
        return error;
      }
      event_info.params = params_ptr.get();
      param_buffers.push_back(std::move(params_ptr));

      for (jint i = 0; i != event_info.param_count; ++i) {
        event_info.params[i] = params[i].ToParamInfo(env, &char_buffers, &error);
        if (error != OK) {
          return error;
        }
      }
    } else {
      event_info.params = nullptr;
    }

    ext_vector.push_back(event_info);

    return ERR(NONE);
  };

  jvmtiError error;
  error = add_extension(
      ArtJvmtiEvent::kDdmPublishChunk,
      "com.android.art.internal.ddm.publish_chunk_safe",
      "Called when there is new ddms information that the agent or other clients can use. The"
      " agent is given the 'type' of the ddms chunk and a 'data_size' byte-buffer in 'data'."
      " The 'data' pointer is only valid for the duration of the publish_chunk event. The agent"
      " is responsible for interpreting the information present in the 'data' buffer. This is"
      " provided for backwards-compatibility support only. Agents should prefer to use relevant"
      " JVMTI events and functions above listening for this event. Previous publish_chunk"
      " event was inherently unsafe since using the JNIEnv could cause deadlocks in some scenarios."
      " The current version does not have these issues.",
      {
        { "type", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
        { "data_size", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
        { "data",  JVMTI_KIND_IN_BUF, JVMTI_TYPE_JBYTE, false },
      });
  if (error != OK) {
    return error;
  }
  error = add_extension(
      ArtJvmtiEvent::kObsoleteObjectCreated,
      "com.android.art.heap.obsolete_object_created",
      "Called when an obsolete object is created.\n"
      "An object becomes obsolete when, due to some jvmti function call all references to the"
      " object are replaced with a reference to a different object. After this call finishes there"
      " will be no strong references to the obsolete object anywere. If the object is retrieved"
      " using GetObjectsWithTags its type (class) may have changed and any data it contains may"
      " have been deleted. This is primarily designed to support memory tracking agents which make"
      " use of the ObjectFree and VMObjectAlloc events for tracking. To support this use-case if"
      " this event is not being handled it will by default act as though the following code was"
      " registered as a handler:\n"
      "\n"
      "  void HandleObsoleteObjectCreated(jvmtiEnv* env, jlong* obsolete_tag, jlong* new_tag) {\n"
      "    jlong temp = *obsolete_tag;\n"
      "    *obsolete_tag = *new_tag;\n"
      "    *new_tag = temp;\n"
      "  }\n"
      "\n"
      "Note that this event does not support filtering based on thread. This event has the same"
      " restrictions on JNI and JVMTI function calls as the ObjectFree event.\n"
      "\n"
      "Arguments:\n"
      "  obsolete_tag: Pointer to the tag the old object (now obsolete) has. Setting the pointer"
      " will update the tag value.\n"
      "  new_tag: Pointer to the tag the new object (replacing the obsolete one) has. Setting the"
      " pointer will update the tag value.",
      {
        { "obsolete_tag", JVMTI_KIND_IN_PTR, JVMTI_TYPE_JLONG, false },
        { "new_tag", JVMTI_KIND_IN_PTR, JVMTI_TYPE_JLONG, false },
      });
  if (error != OK) {
    return error;
  }
  art::Runtime* runtime = art::Runtime::Current();
  if (runtime->GetJniIdType() == art::JniIdType::kIndices &&
      (runtime->GetInstrumentation()->IsForcedInterpretOnly() || runtime->IsJavaDebuggable())) {
    error = add_extension(
        ArtJvmtiEvent::kStructuralDexFileLoadHook,
        "com.android.art.class.structural_dex_file_load_hook",
        "Called during class load, after a 'RetransformClasses' call, or after a 'RedefineClasses'"
        " call in order to allow the agent to modify the class. This event is called after any"
        " non-can_retransform_classes ClassFileLoadHookEvents and before any"
        " can_retransform_classes ClassFileLoadHookEvents. The transformations applied are"
        " restricted in the same way that transformations applied via the "
        " 'com.android.art.class.structurally_redefine_classes' extension function. The arguments"
        " to the event are identical to the ones in the ClassFileLoadHook and have the same"
        " semantics.",
        {
          { "jni_env", JVMTI_KIND_IN, JVMTI_TYPE_JNIENV, false },
          { "class_being_redefined", JVMTI_KIND_IN, JVMTI_TYPE_JCLASS, true },
          { "loader", JVMTI_KIND_IN, JVMTI_TYPE_JOBJECT, false },
          { "name", JVMTI_KIND_IN_PTR, JVMTI_TYPE_CCHAR, false },
          { "protection_domain", JVMTI_KIND_IN, JVMTI_TYPE_JOBJECT, true },
          { "dex_data_len", JVMTI_KIND_IN, JVMTI_TYPE_JINT, false },
          { "dex_data", JVMTI_KIND_IN_BUF, JVMTI_TYPE_CCHAR, false },
          { "new_dex_data_len", JVMTI_KIND_OUT, JVMTI_TYPE_JINT, false },
          { "new_dex_data", JVMTI_KIND_ALLOC_BUF, JVMTI_TYPE_CCHAR, true },
        });
  } else {
    LOG(INFO) << "debuggable & jni-type indices are required to implement structural "
              << "class redefinition extensions.";
  }
  // Copy into output buffer.

  *extension_count_ptr = ext_vector.size();
  JvmtiUniquePtr<jvmtiExtensionEventInfo[]> out_data =
      AllocJvmtiUniquePtr<jvmtiExtensionEventInfo[]>(env, ext_vector.size(), &error);
  if (out_data == nullptr) {
    return error;
  }
  memcpy(out_data.get(),
         ext_vector.data(),
         ext_vector.size() * sizeof(jvmtiExtensionEventInfo));
  *extensions = out_data.release();

  // Release all the buffer holders, we're OK now.
  for (auto& holder : char_buffers) {
    holder.release();
  }
  for (auto& holder : param_buffers) {
    holder.release();
  }

  return OK;
}

jvmtiError ExtensionUtil::SetExtensionEventCallback(jvmtiEnv* env,
                                                    jint extension_event_index,
                                                    jvmtiExtensionEvent callback,
                                                    EventHandler* event_handler) {
  if (!IsExtensionEvent(extension_event_index)) {
    return ERR(ILLEGAL_ARGUMENT);
  }
  ArtJvmTiEnv* art_env = ArtJvmTiEnv::AsArtJvmTiEnv(env);
  jvmtiEventMode mode = callback == nullptr ? JVMTI_DISABLE : JVMTI_ENABLE;
  // Lock the event_info_mutex_ while we set the event to make sure it isn't lost by a concurrent
  // change to the normal callbacks.
  {
    art::WriterMutexLock lk(art::Thread::Current(), art_env->event_info_mutex_);
    if (art_env->event_callbacks.get() == nullptr) {
      art_env->event_callbacks.reset(new ArtJvmtiEventCallbacks());
    }
    jvmtiError err = art_env->event_callbacks->Set(extension_event_index, callback);
    if (err != OK) {
      return err;
    }
  }
  return event_handler->SetEvent(art_env,
                                 /*thread=*/nullptr,
                                 static_cast<ArtJvmtiEvent>(extension_event_index),
                                 mode);
}

}  // namespace openjdkjvmti
