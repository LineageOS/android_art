/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef ART_RUNTIME_CLASS_ROOT_H_
#define ART_RUNTIME_CLASS_ROOT_H_

#include <stdint.h>

#include "base/locks.h"
#include "read_barrier_option.h"

namespace art {

class ClassLinker;
template<class MirrorType> class ObjPtr;

namespace mirror {
class ArrayElementVarHandle;
class ByteArrayViewVarHandle;
class ByteBufferViewVarHandle;
class CallSite;
class Class;
class ClassExt;
class ClassLoader;
class Constructor;
class DexCache;
class EmulatedStackFrame;
class Field;
class FieldVarHandle;
class Method;
class MethodHandleImpl;
class MethodHandlesLookup;
class MethodType;
class Object;
template<class T> class ObjectArray;
class Proxy;
template<typename T> class PrimitiveArray;
class Reference;
class StackTraceElement;
class String;
class Throwable;
class VarHandle;
}  // namespace mirror

#define CLASS_MIRROR_ROOT_LIST(M)                                                                                                         \
  M(kJavaLangClass,                         "Ljava/lang/Class;",                          mirror::Class)                                  \
  M(kJavaLangObject,                        "Ljava/lang/Object;",                         mirror::Object)                                 \
  M(kClassArrayClass,                       "[Ljava/lang/Class;",                         mirror::ObjectArray<mirror::Class>)             \
  M(kObjectArrayClass,                      "[Ljava/lang/Object;",                        mirror::ObjectArray<mirror::Object>)            \
  M(kJavaLangString,                        "Ljava/lang/String;",                         mirror::String)                                 \
  M(kJavaLangDexCache,                      "Ljava/lang/DexCache;",                       mirror::DexCache)                               \
  M(kJavaLangRefReference,                  "Ljava/lang/ref/Reference;",                  mirror::Reference)                              \
  M(kJavaLangReflectConstructor,            "Ljava/lang/reflect/Constructor;",            mirror::Constructor)                            \
  M(kJavaLangReflectField,                  "Ljava/lang/reflect/Field;",                  mirror::Field)                                  \
  M(kJavaLangReflectMethod,                 "Ljava/lang/reflect/Method;",                 mirror::Method)                                 \
  M(kJavaLangReflectProxy,                  "Ljava/lang/reflect/Proxy;",                  mirror::Proxy)                                  \
  M(kJavaLangStringArrayClass,              "[Ljava/lang/String;",                        mirror::ObjectArray<mirror::String>)            \
  M(kJavaLangReflectConstructorArrayClass,  "[Ljava/lang/reflect/Constructor;",           mirror::ObjectArray<mirror::Constructor>)       \
  M(kJavaLangReflectFieldArrayClass,        "[Ljava/lang/reflect/Field;",                 mirror::ObjectArray<mirror::Field>)             \
  M(kJavaLangReflectMethodArrayClass,       "[Ljava/lang/reflect/Method;",                mirror::ObjectArray<mirror::Method>)            \
  M(kJavaLangInvokeCallSite,                "Ljava/lang/invoke/CallSite;",                mirror::CallSite)                               \
  M(kJavaLangInvokeMethodHandle,            "Ljava/lang/invoke/MethodHandle;",            mirror::MethodHandle)                           \
  M(kJavaLangInvokeMethodHandleImpl,        "Ljava/lang/invoke/MethodHandleImpl;",        mirror::MethodHandleImpl)                       \
  M(kJavaLangInvokeMethodHandlesLookup,     "Ljava/lang/invoke/MethodHandles$Lookup;",    mirror::MethodHandlesLookup)                    \
  M(kJavaLangInvokeMethodType,              "Ljava/lang/invoke/MethodType;",              mirror::MethodType)                             \
  M(kJavaLangInvokeVarHandle,               "Ljava/lang/invoke/VarHandle;",               mirror::VarHandle)                              \
  M(kJavaLangInvokeFieldVarHandle,          "Ljava/lang/invoke/FieldVarHandle;",          mirror::FieldVarHandle)                         \
  M(kJavaLangInvokeArrayElementVarHandle,   "Ljava/lang/invoke/ArrayElementVarHandle;",   mirror::ArrayElementVarHandle)                  \
  M(kJavaLangInvokeByteArrayViewVarHandle,  "Ljava/lang/invoke/ByteArrayViewVarHandle;",  mirror::ByteArrayViewVarHandle)                 \
  M(kJavaLangInvokeByteBufferViewVarHandle, "Ljava/lang/invoke/ByteBufferViewVarHandle;", mirror::ByteBufferViewVarHandle)                \
  M(kJavaLangClassLoader,                   "Ljava/lang/ClassLoader;",                    mirror::ClassLoader)                            \
  M(kJavaLangThrowable,                     "Ljava/lang/Throwable;",                      mirror::Throwable)                              \
  M(kJavaLangStackTraceElement,             "Ljava/lang/StackTraceElement;",              mirror::StackTraceElement)                      \
  M(kDalvikSystemEmulatedStackFrame,        "Ldalvik/system/EmulatedStackFrame;",         mirror::EmulatedStackFrame)                     \
  M(kBooleanArrayClass,                     "[Z",                                         mirror::PrimitiveArray<uint8_t>)                \
  M(kByteArrayClass,                        "[B",                                         mirror::PrimitiveArray<int8_t>)                 \
  M(kCharArrayClass,                        "[C",                                         mirror::PrimitiveArray<uint16_t>)               \
  M(kDoubleArrayClass,                      "[D",                                         mirror::PrimitiveArray<double>)                 \
  M(kFloatArrayClass,                       "[F",                                         mirror::PrimitiveArray<float>)                  \
  M(kIntArrayClass,                         "[I",                                         mirror::PrimitiveArray<int32_t>)                \
  M(kLongArrayClass,                        "[J",                                         mirror::PrimitiveArray<int64_t>)                \
  M(kShortArrayClass,                       "[S",                                         mirror::PrimitiveArray<int16_t>)                \
  M(kJavaLangStackTraceElementArrayClass,   "[Ljava/lang/StackTraceElement;",             mirror::ObjectArray<mirror::StackTraceElement>) \
  M(kJavaLangClassLoaderArrayClass,         "[Ljava/lang/ClassLoader;",                   mirror::ObjectArray<mirror::ClassLoader>)       \
  M(kDalvikSystemClassExt,                  "Ldalvik/system/ClassExt;",                   mirror::ClassExt)

#define CLASS_NO_MIRROR_ROOT_LIST(M)                                                                                                                \
  M(kJavaLangClassNotFoundException,        "Ljava/lang/ClassNotFoundException;",         detail::NoMirrorType<detail::ClassNotFoundExceptionTag>)  \
  M(kPrimitiveBoolean,                      "Z",                                          detail::NoMirrorType<uint8_t>)                            \
  M(kPrimitiveByte,                         "B",                                          detail::NoMirrorType<int8_t>)                             \
  M(kPrimitiveChar,                         "C",                                          detail::NoMirrorType<uint16_t>)                           \
  M(kPrimitiveDouble,                       "D",                                          detail::NoMirrorType<double>)                             \
  M(kPrimitiveFloat,                        "F",                                          detail::NoMirrorType<float>)                              \
  M(kPrimitiveInt,                          "I",                                          detail::NoMirrorType<int32_t>)                            \
  M(kPrimitiveLong,                         "J",                                          detail::NoMirrorType<int64_t>)                            \
  M(kPrimitiveShort,                        "S",                                          detail::NoMirrorType<int16_t>)                            \
  M(kPrimitiveVoid,                         "V",                                          detail::NoMirrorType<void>)

#define CLASS_ROOT_LIST(M)     \
  CLASS_MIRROR_ROOT_LIST(M)    \
  CLASS_NO_MIRROR_ROOT_LIST(M)

// Well known mirror::Class roots accessed via ClassLinker::GetClassRoots().
enum class ClassRoot : uint32_t {
#define CLASS_ROOT_ENUMERATOR(name, descriptor, mirror_type) name,
  CLASS_ROOT_LIST(CLASS_ROOT_ENUMERATOR)
#undef CLASS_ROOT_ENUMERATOR
  kMax,
};

const char* GetClassRootDescriptor(ClassRoot class_root);

template <ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
ObjPtr<mirror::Class> GetClassRoot(ClassRoot class_root,
                                   ObjPtr<mirror::ObjectArray<mirror::Class>> class_roots)
    REQUIRES_SHARED(Locks::mutator_lock_);

template <ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
ObjPtr<mirror::Class> GetClassRoot(ClassRoot class_root, ClassLinker* linker)
    REQUIRES_SHARED(Locks::mutator_lock_);

template <ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
ObjPtr<mirror::Class> GetClassRoot(ClassRoot class_root) REQUIRES_SHARED(Locks::mutator_lock_);

template <class MirrorType, ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
ObjPtr<mirror::Class> GetClassRoot(ObjPtr<mirror::ObjectArray<mirror::Class>> class_roots)
    REQUIRES_SHARED(Locks::mutator_lock_);

template <class MirrorType, ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
ObjPtr<mirror::Class> GetClassRoot(ClassLinker* linker) REQUIRES_SHARED(Locks::mutator_lock_);

template <class MirrorType, ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
ObjPtr<mirror::Class> GetClassRoot() REQUIRES_SHARED(Locks::mutator_lock_);

}  // namespace art

#endif  // ART_RUNTIME_CLASS_ROOT_H_
