/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "dalvik_system_VMRuntime.h"

#ifdef ART_TARGET_ANDROID
#include <sys/resource.h>
#include <sys/time.h>
extern "C" void android_set_application_target_sdk_version(uint32_t version);
#endif
#include <inttypes.h>
#include <limits>
#include <limits.h>
#include "nativehelper/scoped_utf_chars.h"

#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "arch/instruction_set.h"
#include "art_method-inl.h"
#include "base/enums.h"
#include "base/sdk_version.h"
#include "class_linker-inl.h"
#include "class_loader_context.h"
#include "common_throws.h"
#include "debugger.h"
#include "dex/class_accessor-inl.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_types.h"
#include "gc/accounting/card_table-inl.h"
#include "gc/allocator/dlmalloc.h"
#include "gc/heap.h"
#include "gc/space/dlmalloc_space.h"
#include "gc/space/image_space.h"
#include "gc/task_processor.h"
#include "intern_table.h"
#include "jit/jit.h"
#include "jni/java_vm_ext.h"
#include "jni/jni_internal.h"
#include "mirror/array-alloc-inl.h"
#include "mirror/class-inl.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/object-inl.h"
#include "native_util.h"
#include "nativehelper/jni_macros.h"
#include "nativehelper/scoped_local_ref.h"
#include "runtime.h"
#include "scoped_fast_native_object_access-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "thread_list.h"
#include "well_known_classes.h"

namespace art {

using android::base::StringPrintf;

static jfloat VMRuntime_getTargetHeapUtilization(JNIEnv*, jobject) {
  return Runtime::Current()->GetHeap()->GetTargetHeapUtilization();
}

static void VMRuntime_nativeSetTargetHeapUtilization(JNIEnv*, jobject, jfloat target) {
  Runtime::Current()->GetHeap()->SetTargetHeapUtilization(target);
}

static void VMRuntime_setHiddenApiExemptions(JNIEnv* env,
                                            jclass,
                                            jobjectArray exemptions) {
  std::vector<std::string> exemptions_vec;
  int exemptions_length = env->GetArrayLength(exemptions);
  for (int i = 0; i < exemptions_length; i++) {
    jstring exemption = reinterpret_cast<jstring>(env->GetObjectArrayElement(exemptions, i));
    const char* raw_exemption = env->GetStringUTFChars(exemption, nullptr);
    exemptions_vec.push_back(raw_exemption);
    env->ReleaseStringUTFChars(exemption, raw_exemption);
  }

  Runtime::Current()->SetHiddenApiExemptions(exemptions_vec);
}

static void VMRuntime_setHiddenApiAccessLogSamplingRate(JNIEnv*, jclass, jint rate) {
  Runtime::Current()->SetHiddenApiEventLogSampleRate(rate);
}

static jobject VMRuntime_newNonMovableArray(JNIEnv* env, jobject, jclass javaElementClass,
                                            jint length) {
  ScopedFastNativeObjectAccess soa(env);
  if (UNLIKELY(length < 0)) {
    ThrowNegativeArraySizeException(length);
    return nullptr;
  }
  ObjPtr<mirror::Class> element_class = soa.Decode<mirror::Class>(javaElementClass);
  if (UNLIKELY(element_class == nullptr)) {
    ThrowNullPointerException("element class == null");
    return nullptr;
  }
  Runtime* runtime = Runtime::Current();
  ObjPtr<mirror::Class> array_class =
      runtime->GetClassLinker()->FindArrayClass(soa.Self(), element_class);
  if (UNLIKELY(array_class == nullptr)) {
    return nullptr;
  }
  gc::AllocatorType allocator = runtime->GetHeap()->GetCurrentNonMovingAllocator();
  ObjPtr<mirror::Array> result = mirror::Array::Alloc(soa.Self(),
                                                      array_class,
                                                      length,
                                                      array_class->GetComponentSizeShift(),
                                                      allocator);
  return soa.AddLocalReference<jobject>(result);
}

static jobject VMRuntime_newUnpaddedArray(JNIEnv* env, jobject, jclass javaElementClass,
                                          jint length) {
  ScopedFastNativeObjectAccess soa(env);
  if (UNLIKELY(length < 0)) {
    ThrowNegativeArraySizeException(length);
    return nullptr;
  }
  ObjPtr<mirror::Class> element_class = soa.Decode<mirror::Class>(javaElementClass);
  if (UNLIKELY(element_class == nullptr)) {
    ThrowNullPointerException("element class == null");
    return nullptr;
  }
  Runtime* runtime = Runtime::Current();
  ObjPtr<mirror::Class> array_class = runtime->GetClassLinker()->FindArrayClass(soa.Self(),
                                                                                element_class);
  if (UNLIKELY(array_class == nullptr)) {
    return nullptr;
  }
  gc::AllocatorType allocator = runtime->GetHeap()->GetCurrentAllocator();
  ObjPtr<mirror::Array> result =
      mirror::Array::Alloc</*kIsInstrumented=*/ true, /*kFillUsable=*/ true>(
          soa.Self(),
          array_class,
          length,
          array_class->GetComponentSizeShift(),
          allocator);
  return soa.AddLocalReference<jobject>(result);
}

static jlong VMRuntime_addressOf(JNIEnv* env, jobject, jobject javaArray) {
  if (javaArray == nullptr) {  // Most likely allocation failed
    return 0;
  }
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Array> array = soa.Decode<mirror::Array>(javaArray);
  if (!array->IsArrayInstance()) {
    ThrowIllegalArgumentException("not an array");
    return 0;
  }
  if (Runtime::Current()->GetHeap()->IsMovableObject(array)) {
    ThrowRuntimeException("Trying to get address of movable array object");
    return 0;
  }
  return reinterpret_cast<uintptr_t>(array->GetRawData(array->GetClass()->GetComponentSize(), 0));
}

static void VMRuntime_clearGrowthLimit(JNIEnv*, jobject) {
  Runtime::Current()->GetHeap()->ClearGrowthLimit();
}

static void VMRuntime_clampGrowthLimit(JNIEnv*, jobject) {
  Runtime::Current()->GetHeap()->ClampGrowthLimit();
}

static jboolean VMRuntime_isNativeDebuggable(JNIEnv*, jobject) {
  return Runtime::Current()->IsNativeDebuggable();
}

static jboolean VMRuntime_isJavaDebuggable(JNIEnv*, jobject) {
  return Runtime::Current()->IsJavaDebuggable();
}

static jobjectArray VMRuntime_properties(JNIEnv* env, jobject) {
  DCHECK(WellKnownClasses::java_lang_String != nullptr);

  const std::vector<std::string>& properties = Runtime::Current()->GetProperties();
  ScopedLocalRef<jobjectArray> ret(env,
                                   env->NewObjectArray(static_cast<jsize>(properties.size()),
                                                       WellKnownClasses::java_lang_String,
                                                       nullptr /* initial element */));
  if (ret == nullptr) {
    DCHECK(env->ExceptionCheck());
    return nullptr;
  }
  for (size_t i = 0; i != properties.size(); ++i) {
    ScopedLocalRef<jstring> str(env, env->NewStringUTF(properties[i].c_str()));
    if (str == nullptr) {
      DCHECK(env->ExceptionCheck());
      return nullptr;
    }
    env->SetObjectArrayElement(ret.get(), static_cast<jsize>(i), str.get());
    DCHECK(!env->ExceptionCheck());
  }
  return ret.release();
}

// This is for backward compatibility with dalvik which returned the
// meaningless "." when no boot classpath or classpath was
// specified. Unfortunately, some tests were using java.class.path to
// lookup relative file locations, so they are counting on this to be
// ".", presumably some applications or libraries could have as well.
static const char* DefaultToDot(const std::string& class_path) {
  return class_path.empty() ? "." : class_path.c_str();
}

static jstring VMRuntime_bootClassPath(JNIEnv* env, jobject) {
  std::string boot_class_path = android::base::Join(Runtime::Current()->GetBootClassPath(), ':');
  return env->NewStringUTF(DefaultToDot(boot_class_path));
}

static jstring VMRuntime_classPath(JNIEnv* env, jobject) {
  return env->NewStringUTF(DefaultToDot(Runtime::Current()->GetClassPathString()));
}

static jstring VMRuntime_vmVersion(JNIEnv* env, jobject) {
  return env->NewStringUTF(Runtime::GetVersion());
}

static jstring VMRuntime_vmLibrary(JNIEnv* env, jobject) {
  return env->NewStringUTF(kIsDebugBuild ? "libartd.so" : "libart.so");
}

static jstring VMRuntime_vmInstructionSet(JNIEnv* env, jobject) {
  InstructionSet isa = Runtime::Current()->GetInstructionSet();
  const char* isa_string = GetInstructionSetString(isa);
  return env->NewStringUTF(isa_string);
}

static jboolean VMRuntime_is64Bit(JNIEnv*, jobject) {
  bool is64BitMode = (sizeof(void*) == sizeof(uint64_t));
  return is64BitMode ? JNI_TRUE : JNI_FALSE;
}

static jboolean VMRuntime_isCheckJniEnabled(JNIEnv* env, jobject) {
  return down_cast<JNIEnvExt*>(env)->GetVm()->IsCheckJniEnabled() ? JNI_TRUE : JNI_FALSE;
}

static void VMRuntime_setTargetSdkVersionNative(JNIEnv*, jobject, jint target_sdk_version) {
  // This is the target SDK version of the app we're about to run. It is intended that this a place
  // where workarounds can be enabled.
  // Note that targetSdkVersion may be CUR_DEVELOPMENT (10000).
  // Note that targetSdkVersion may be 0, meaning "current".
  uint32_t uint_target_sdk_version =
      target_sdk_version <= 0 ? static_cast<uint32_t>(SdkVersion::kUnset)
                              : static_cast<uint32_t>(target_sdk_version);
  Runtime::Current()->SetTargetSdkVersion(uint_target_sdk_version);

#ifdef ART_TARGET_ANDROID
  // This part is letting libc/dynamic linker know about current app's
  // target sdk version to enable compatibility workarounds.
  android_set_application_target_sdk_version(uint_target_sdk_version);
#endif
}

static void VMRuntime_setDisabledCompatChangesNative(JNIEnv* env, jobject,
    jlongArray disabled_compat_changes) {
  if (disabled_compat_changes == nullptr) {
    return;
  }
  std::set<uint64_t> disabled_compat_changes_set;
  int length = env->GetArrayLength(disabled_compat_changes);
  jlong* elements = env->GetLongArrayElements(disabled_compat_changes, /*isCopy*/nullptr);
  for (int i = 0; i < length; i++) {
    disabled_compat_changes_set.insert(static_cast<uint64_t>(elements[i]));
  }
  Runtime::Current()->GetCompatFramework().SetDisabledCompatChanges(disabled_compat_changes_set);
}

static inline size_t clamp_to_size_t(jlong n) {
  if (sizeof(jlong) > sizeof(size_t)
      && UNLIKELY(n > static_cast<jlong>(std::numeric_limits<size_t>::max()))) {
    return std::numeric_limits<size_t>::max();
  } else {
    return n;
  }
}

static void VMRuntime_registerNativeAllocation(JNIEnv* env, jobject, jlong bytes) {
  if (UNLIKELY(bytes < 0)) {
    ScopedObjectAccess soa(env);
    ThrowRuntimeException("allocation size negative %" PRId64, bytes);
    return;
  }
  Runtime::Current()->GetHeap()->RegisterNativeAllocation(env, clamp_to_size_t(bytes));
}

static void VMRuntime_registerNativeFree(JNIEnv* env, jobject, jlong bytes) {
  if (UNLIKELY(bytes < 0)) {
    ScopedObjectAccess soa(env);
    ThrowRuntimeException("allocation size negative %" PRId64, bytes);
    return;
  }
  Runtime::Current()->GetHeap()->RegisterNativeFree(env, clamp_to_size_t(bytes));
}

static jint VMRuntime_getNotifyNativeInterval(JNIEnv*, jclass) {
  return Runtime::Current()->GetHeap()->GetNotifyNativeInterval();
}

static void VMRuntime_notifyNativeAllocationsInternal(JNIEnv* env, jobject) {
  Runtime::Current()->GetHeap()->NotifyNativeAllocations(env);
}

static jlong VMRuntime_getFinalizerTimeoutMs(JNIEnv*, jobject) {
  return Runtime::Current()->GetFinalizerTimeoutMs();
}

static void VMRuntime_registerSensitiveThread(JNIEnv*, jobject) {
  Runtime::Current()->RegisterSensitiveThread();
}

static void VMRuntime_updateProcessState(JNIEnv*, jobject, jint process_state) {
  Runtime* runtime = Runtime::Current();
  runtime->UpdateProcessState(static_cast<ProcessState>(process_state));
}

static void VMRuntime_notifyStartupCompleted(JNIEnv*, jobject) {
  Runtime::Current()->NotifyStartupCompleted();
}

static void VMRuntime_trimHeap(JNIEnv* env, jobject) {
  Runtime::Current()->GetHeap()->Trim(ThreadForEnv(env));
}

static void VMRuntime_requestHeapTrim(JNIEnv* env, jobject) {
  Runtime::Current()->GetHeap()->RequestTrim(ThreadForEnv(env));
}

static void VMRuntime_requestConcurrentGC(JNIEnv* env, jobject) {
  gc::Heap *heap = Runtime::Current()->GetHeap();
  heap->RequestConcurrentGC(ThreadForEnv(env),
                            gc::kGcCauseBackground,
                            true,
                            heap->GetCurrentGcNum());
}

static void VMRuntime_startHeapTaskProcessor(JNIEnv* env, jobject) {
  Runtime::Current()->GetHeap()->GetTaskProcessor()->Start(ThreadForEnv(env));
}

static void VMRuntime_stopHeapTaskProcessor(JNIEnv* env, jobject) {
  Runtime::Current()->GetHeap()->GetTaskProcessor()->Stop(ThreadForEnv(env));
}

static void VMRuntime_runHeapTasks(JNIEnv* env, jobject) {
  Runtime::Current()->GetHeap()->GetTaskProcessor()->RunAllTasks(ThreadForEnv(env));
}

static void VMRuntime_preloadDexCaches(JNIEnv* env ATTRIBUTE_UNUSED, jobject) {
}

/*
 * This is called by the framework when it knows the application directory and
 * process name.
 */
static void VMRuntime_registerAppInfo(JNIEnv* env,
                                      jclass clazz ATTRIBUTE_UNUSED,
                                      jstring profile_file,
                                      jobjectArray code_paths) {
  std::vector<std::string> code_paths_vec;
  int code_paths_length = env->GetArrayLength(code_paths);
  for (int i = 0; i < code_paths_length; i++) {
    jstring code_path = reinterpret_cast<jstring>(env->GetObjectArrayElement(code_paths, i));
    const char* raw_code_path = env->GetStringUTFChars(code_path, nullptr);
    code_paths_vec.push_back(raw_code_path);
    env->ReleaseStringUTFChars(code_path, raw_code_path);
  }

  const char* raw_profile_file = env->GetStringUTFChars(profile_file, nullptr);
  std::string profile_file_str(raw_profile_file);
  env->ReleaseStringUTFChars(profile_file, raw_profile_file);

  Runtime::Current()->RegisterAppInfo(code_paths_vec, profile_file_str);
}

static jboolean VMRuntime_isBootClassPathOnDisk(JNIEnv* env, jclass, jstring java_instruction_set) {
  ScopedUtfChars instruction_set(env, java_instruction_set);
  if (instruction_set.c_str() == nullptr) {
    return JNI_FALSE;
  }
  InstructionSet isa = GetInstructionSetFromString(instruction_set.c_str());
  if (isa == InstructionSet::kNone) {
    ScopedLocalRef<jclass> iae(env, env->FindClass("java/lang/IllegalArgumentException"));
    std::string message(StringPrintf("Instruction set %s is invalid.", instruction_set.c_str()));
    env->ThrowNew(iae.get(), message.c_str());
    return JNI_FALSE;
  }
  return gc::space::ImageSpace::IsBootClassPathOnDisk(isa);
}

static jstring VMRuntime_getCurrentInstructionSet(JNIEnv* env, jclass) {
  return env->NewStringUTF(GetInstructionSetString(kRuntimeISA));
}

static void VMRuntime_setSystemDaemonThreadPriority(JNIEnv* env ATTRIBUTE_UNUSED,
                                                    jclass klass ATTRIBUTE_UNUSED) {
#ifdef ART_TARGET_ANDROID
  Thread* self = Thread::Current();
  DCHECK(self != nullptr);
  pid_t tid = self->GetTid();
  // We use a priority lower than the default for the system daemon threads (eg HeapTaskDaemon) to
  // avoid jank due to CPU contentions between GC and other UI-related threads. b/36631902.
  // We may use a native priority that doesn't have a corresponding java.lang.Thread-level priority.
  static constexpr int kSystemDaemonNiceValue = 4;  // priority 124
  if (setpriority(PRIO_PROCESS, tid, kSystemDaemonNiceValue) != 0) {
    PLOG(INFO) << *self << " setpriority(PRIO_PROCESS, " << tid << ", "
               << kSystemDaemonNiceValue << ") failed";
  }
#endif
}

static void VMRuntime_setDedupeHiddenApiWarnings(JNIEnv* env ATTRIBUTE_UNUSED,
                                                 jclass klass ATTRIBUTE_UNUSED,
                                                 jboolean dedupe) {
  Runtime::Current()->SetDedupeHiddenApiWarnings(dedupe);
}

static void VMRuntime_setProcessPackageName(JNIEnv* env,
                                            jclass klass ATTRIBUTE_UNUSED,
                                            jstring java_package_name) {
  ScopedUtfChars package_name(env, java_package_name);
  Runtime::Current()->SetProcessPackageName(package_name.c_str());
}

static void VMRuntime_setProcessDataDirectory(JNIEnv* env, jclass, jstring java_data_dir) {
  ScopedUtfChars data_dir(env, java_data_dir);
  Runtime::Current()->SetProcessDataDirectory(data_dir.c_str());
}

static void VMRuntime_bootCompleted(JNIEnv* env ATTRIBUTE_UNUSED,
                                    jclass klass ATTRIBUTE_UNUSED) {
  jit::Jit* jit = Runtime::Current()->GetJit();
  if (jit != nullptr) {
    jit->BootCompleted();
  }
}

class ClearJitCountersVisitor : public ClassVisitor {
 public:
  bool operator()(ObjPtr<mirror::Class> klass) override REQUIRES_SHARED(Locks::mutator_lock_) {
    // Avoid some types of classes that don't need their methods visited.
    if (klass->IsProxyClass() ||
        klass->IsArrayClass() ||
        klass->IsPrimitive() ||
        !klass->IsResolved() ||
        klass->IsErroneousResolved()) {
      return true;
    }
    for (ArtMethod& m : klass->GetMethods(kRuntimePointerSize)) {
      if (!m.IsAbstract()) {
        if (m.GetCounter() != 0) {
          m.SetCounter(0);
        }
      }
    }
    return true;
  }
};

static void VMRuntime_resetJitCounters(JNIEnv* env, jclass klass ATTRIBUTE_UNUSED) {
  ScopedObjectAccess soa(env);
  ClearJitCountersVisitor visitor;
  Runtime::Current()->GetClassLinker()->VisitClasses(&visitor);
}

static jboolean VMRuntime_isValidClassLoaderContext(JNIEnv* env,
                                                    jclass klass ATTRIBUTE_UNUSED,
                                                    jstring jencoded_class_loader_context) {
  if (UNLIKELY(jencoded_class_loader_context == nullptr)) {
    ScopedFastNativeObjectAccess soa(env);
    ThrowNullPointerException("encoded_class_loader_context == null");
    return false;
  }
  ScopedUtfChars encoded_class_loader_context(env, jencoded_class_loader_context);
  return ClassLoaderContext::IsValidEncoding(encoded_class_loader_context.c_str());
}

static JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(VMRuntime, addressOf, "(Ljava/lang/Object;)J"),
  NATIVE_METHOD(VMRuntime, bootClassPath, "()Ljava/lang/String;"),
  NATIVE_METHOD(VMRuntime, clampGrowthLimit, "()V"),
  NATIVE_METHOD(VMRuntime, classPath, "()Ljava/lang/String;"),
  NATIVE_METHOD(VMRuntime, clearGrowthLimit, "()V"),
  NATIVE_METHOD(VMRuntime, setHiddenApiExemptions, "([Ljava/lang/String;)V"),
  NATIVE_METHOD(VMRuntime, setHiddenApiAccessLogSamplingRate, "(I)V"),
  NATIVE_METHOD(VMRuntime, getTargetHeapUtilization, "()F"),
  FAST_NATIVE_METHOD(VMRuntime, isNativeDebuggable, "()Z"),
  NATIVE_METHOD(VMRuntime, isJavaDebuggable, "()Z"),
  NATIVE_METHOD(VMRuntime, nativeSetTargetHeapUtilization, "(F)V"),
  FAST_NATIVE_METHOD(VMRuntime, newNonMovableArray, "(Ljava/lang/Class;I)Ljava/lang/Object;"),
  FAST_NATIVE_METHOD(VMRuntime, newUnpaddedArray, "(Ljava/lang/Class;I)Ljava/lang/Object;"),
  NATIVE_METHOD(VMRuntime, properties, "()[Ljava/lang/String;"),
  NATIVE_METHOD(VMRuntime, setTargetSdkVersionNative, "(I)V"),
  NATIVE_METHOD(VMRuntime, setDisabledCompatChangesNative, "([J)V"),
  NATIVE_METHOD(VMRuntime, registerNativeAllocation, "(J)V"),
  NATIVE_METHOD(VMRuntime, registerNativeFree, "(J)V"),
  NATIVE_METHOD(VMRuntime, getNotifyNativeInterval, "()I"),
  NATIVE_METHOD(VMRuntime, getFinalizerTimeoutMs, "()J"),
  NATIVE_METHOD(VMRuntime, notifyNativeAllocationsInternal, "()V"),
  NATIVE_METHOD(VMRuntime, notifyStartupCompleted, "()V"),
  NATIVE_METHOD(VMRuntime, registerSensitiveThread, "()V"),
  NATIVE_METHOD(VMRuntime, requestConcurrentGC, "()V"),
  NATIVE_METHOD(VMRuntime, requestHeapTrim, "()V"),
  NATIVE_METHOD(VMRuntime, runHeapTasks, "()V"),
  NATIVE_METHOD(VMRuntime, updateProcessState, "(I)V"),
  NATIVE_METHOD(VMRuntime, startHeapTaskProcessor, "()V"),
  NATIVE_METHOD(VMRuntime, stopHeapTaskProcessor, "()V"),
  NATIVE_METHOD(VMRuntime, trimHeap, "()V"),
  NATIVE_METHOD(VMRuntime, vmVersion, "()Ljava/lang/String;"),
  NATIVE_METHOD(VMRuntime, vmLibrary, "()Ljava/lang/String;"),
  NATIVE_METHOD(VMRuntime, vmInstructionSet, "()Ljava/lang/String;"),
  FAST_NATIVE_METHOD(VMRuntime, is64Bit, "()Z"),
  FAST_NATIVE_METHOD(VMRuntime, isCheckJniEnabled, "()Z"),
  NATIVE_METHOD(VMRuntime, preloadDexCaches, "()V"),
  NATIVE_METHOD(VMRuntime, registerAppInfo, "(Ljava/lang/String;[Ljava/lang/String;)V"),
  NATIVE_METHOD(VMRuntime, isBootClassPathOnDisk, "(Ljava/lang/String;)Z"),
  NATIVE_METHOD(VMRuntime, getCurrentInstructionSet, "()Ljava/lang/String;"),
  NATIVE_METHOD(VMRuntime, setSystemDaemonThreadPriority, "()V"),
  NATIVE_METHOD(VMRuntime, setDedupeHiddenApiWarnings, "(Z)V"),
  NATIVE_METHOD(VMRuntime, setProcessPackageName, "(Ljava/lang/String;)V"),
  NATIVE_METHOD(VMRuntime, setProcessDataDirectory, "(Ljava/lang/String;)V"),
  NATIVE_METHOD(VMRuntime, bootCompleted, "()V"),
  NATIVE_METHOD(VMRuntime, resetJitCounters, "()V"),
  NATIVE_METHOD(VMRuntime, isValidClassLoaderContext, "(Ljava/lang/String;)Z"),
};

void register_dalvik_system_VMRuntime(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("dalvik/system/VMRuntime");
}

}  // namespace art
