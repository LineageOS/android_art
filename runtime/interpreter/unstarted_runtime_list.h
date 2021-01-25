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

#ifndef ART_RUNTIME_INTERPRETER_UNSTARTED_RUNTIME_LIST_H_
#define ART_RUNTIME_INTERPRETER_UNSTARTED_RUNTIME_LIST_H_

// Methods that intercept available libcore implementations.
#define UNSTARTED_RUNTIME_DIRECT_LIST(V)    \
  V(CharacterToLowerCase, "Ljava/lang/Character;", "toLowerCase", "(I)I") \
  V(CharacterToUpperCase, "Ljava/lang/Character;", "toUpperCase", "(I)I") \
  V(ClassForName, "Ljava/lang/Class;", "forName", "(Ljava/lang/String;)Ljava/lang/Class;") \
  V(ClassForNameLong, "Ljava/lang/Class;", "forName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") \
  V(ClassGetPrimitiveClass, "Ljava/lang/Class;", "getPrimitiveClass", "(Ljava/lang/String;)Ljava/lang/Class;") \
  V(ClassClassForName, "Ljava/lang/Class;", "classForName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") \
  V(ClassNewInstance, "Ljava/lang/Class;", "newInstance", "()Ljava/lang/Object;") \
  V(ClassGetDeclaredField, "Ljava/lang/Class;", "getDeclaredField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;") \
  V(ClassGetDeclaredMethod, "Ljava/lang/Class;", "getDeclaredMethodInternal", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;") \
  V(ClassGetDeclaredConstructor, "Ljava/lang/Class;", "getDeclaredConstructorInternal", "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;") \
  V(ClassGetDeclaringClass, "Ljava/lang/Class;", "getDeclaringClass", "()Ljava/lang/Class;") \
  V(ClassGetEnclosingClass, "Ljava/lang/Class;", "getEnclosingClass", "()Ljava/lang/Class;") \
  V(ClassGetInnerClassFlags, "Ljava/lang/Class;", "getInnerClassFlags", "(I)I") \
  V(ClassGetSignatureAnnotation, "Ljava/lang/Class;", "getSignatureAnnotation", "()[Ljava/lang/String;") \
  V(ClassIsAnonymousClass, "Ljava/lang/Class;", "isAnonymousClass", "()Z") \
  V(ClassLoaderGetResourceAsStream, "Ljava/lang/ClassLoader;", "getResourceAsStream", "(Ljava/lang/String;)Ljava/io/InputStream;") \
  V(ConstructorNewInstance0, "Ljava/lang/reflect/Constructor;", "newInstance0", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VmClassLoaderFindLoadedClass, "Ljava/lang/VMClassLoader;", "findLoadedClass", "(Ljava/lang/ClassLoader;Ljava/lang/String;)Ljava/lang/Class;") \
  V(SystemArraycopy, "Ljava/lang/System;", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V") \
  V(SystemArraycopyByte, "Ljava/lang/System;", "arraycopy", "([BI[BII)V") \
  V(SystemArraycopyChar, "Ljava/lang/System;", "arraycopy", "([CI[CII)V") \
  V(SystemArraycopyInt, "Ljava/lang/System;", "arraycopy", "([II[III)V") \
  V(SystemGetSecurityManager, "Ljava/lang/System;", "getSecurityManager", "()Ljava/lang/SecurityManager;") \
  V(SystemGetProperty, "Ljava/lang/System;", "getProperty", "(Ljava/lang/String;)Ljava/lang/String;") \
  V(SystemGetPropertyWithDefault, "Ljava/lang/System;", "getProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") \
  V(ThreadLocalGet, "Ljava/lang/ThreadLocal;", "get", "()Ljava/lang/Object;") \
  V(MathCeil, "Ljava/lang/Math;", "ceil", "(D)D") \
  V(MathFloor, "Ljava/lang/Math;", "floor", "(D)D") \
  V(MathSin, "Ljava/lang/Math;", "sin", "(D)D") \
  V(MathCos, "Ljava/lang/Math;", "cos", "(D)D") \
  V(MathPow, "Ljava/lang/Math;", "pow", "(DD)D") \
  V(ObjectHashCode, "Ljava/lang/Object;", "hashCode", "()I") \
  V(DoubleDoubleToRawLongBits, "Ljava/lang/Double;", "doubleToRawLongBits", "(D)J") \
  V(MemoryPeekByte, "Llibcore/io/Memory;", "peekByte", "(J)B") \
  V(MemoryPeekShort, "Llibcore/io/Memory;", "peekShortNative", "(J)S") \
  V(MemoryPeekInt, "Llibcore/io/Memory;", "peekIntNative", "(J)I") \
  V(MemoryPeekLong, "Llibcore/io/Memory;", "peekLongNative", "(J)J") \
  V(MemoryPeekByteArray, "Llibcore/io/Memory;", "peekByteArray", "(J[BII)V") \
  V(MethodInvoke, "Ljava/lang/reflect/Method;", "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;") \
  V(ReferenceGetReferent, "Ljava/lang/ref/Reference;", "getReferent", "()Ljava/lang/Object;") \
  V(ReferenceRefersTo, "Ljava/lang/ref/Reference;", "refersTo", "(Ljava/lang/Object;)Z") \
  V(RuntimeAvailableProcessors, "Ljava/lang/Runtime;", "availableProcessors", "()I") \
  V(StringGetCharsNoCheck, "Ljava/lang/String;", "getCharsNoCheck", "(II[CI)V") \
  V(StringCharAt, "Ljava/lang/String;", "charAt", "(I)C") \
  V(StringDoReplace, "Ljava/lang/String;", "doReplace", "(CC)Ljava/lang/String;") \
  V(StringFactoryNewStringFromChars, "Ljava/lang/StringFactory;", "newStringFromChars", "(II[C)Ljava/lang/String;") \
  V(StringFactoryNewStringFromString, "Ljava/lang/StringFactory;", "newStringFromString", "(Ljava/lang/String;)Ljava/lang/String;") \
  V(StringFastSubstring, "Ljava/lang/String;", "fastSubstring", "(II)Ljava/lang/String;") \
  V(StringToCharArray, "Ljava/lang/String;", "toCharArray", "()[C") \
  V(ThreadCurrentThread, "Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;") \
  V(ThreadGetNativeState, "Ljava/lang/Thread;", "nativeGetStatus", "(Z)I") \
  V(UnsafeCompareAndSwapLong, "Lsun/misc/Unsafe;", "compareAndSwapLong", "(Ljava/lang/Object;JJJ)Z") \
  V(UnsafeCompareAndSwapObject, "Lsun/misc/Unsafe;", "compareAndSwapObject", "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z") \
  V(UnsafeGetObjectVolatile, "Lsun/misc/Unsafe;", "getObjectVolatile", "(Ljava/lang/Object;J)Ljava/lang/Object;") \
  V(UnsafePutObjectVolatile, "Lsun/misc/Unsafe;", "putObjectVolatile", "(Ljava/lang/Object;JLjava/lang/Object;)V") \
  V(UnsafePutOrderedObject, "Lsun/misc/Unsafe;", "putOrderedObject", "(Ljava/lang/Object;JLjava/lang/Object;)V") \
  V(IntegerParseInt, "Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I") \
  V(LongParseLong, "Ljava/lang/Long;", "parseLong", "(Ljava/lang/String;)J") \
  V(SystemIdentityHashCode, "Ljava/lang/System;", "identityHashCode", "(Ljava/lang/Object;)I")

// Methods that are native.
#define UNSTARTED_RUNTIME_JNI_LIST(V)           \
  V(VMRuntimeIs64Bit, "Ldalvik/system/VMRuntime;", "is64Bit", "()Z") \
  V(VMRuntimeNewUnpaddedArray, "Ldalvik/system/VMRuntime;", "newUnpaddedArray", "(Ljava/lang/Class;I)Ljava/lang/Object;") \
  V(VMStackGetCallingClassLoader, "Ldalvik/system/VMStack;", "getCallingClassLoader", "()Ljava/lang/ClassLoader;") \
  V(VMStackGetStackClass2, "Ldalvik/system/VMStack;", "getStackClass2", "()Ljava/lang/Class;") \
  V(MathLog, "Ljava/lang/Math;", "log", "(D)D") \
  V(MathExp, "Ljava/lang/Math;", "exp", "(D)D") \
  V(AtomicLongVMSupportsCS8, "Ljava/util/concurrent/atomic/AtomicLong;", "VMSupportsCS8", "()Z") \
  V(ClassGetNameNative, "Ljava/lang/Class;", "getNameNative", "()Ljava/lang/String;") \
  V(DoubleLongBitsToDouble, "Ljava/lang/Double;", "longBitsToDouble", "(J)D") \
  V(FloatFloatToRawIntBits, "Ljava/lang/Float;", "floatToRawIntBits", "(F)I") \
  V(FloatIntBitsToFloat, "Ljava/lang/Float;", "intBitsToFloat", "(I)F") \
  V(ObjectInternalClone, "Ljava/lang/Object;", "internalClone", "()Ljava/lang/Object;") \
  V(ObjectNotifyAll, "Ljava/lang/Object;", "notifyAll", "()V") \
  V(StringCompareTo, "Ljava/lang/String;", "compareTo", "(Ljava/lang/String;)I") \
  V(StringIntern, "Ljava/lang/String;", "intern", "()Ljava/lang/String;") \
  V(ArrayCreateMultiArray, "Ljava/lang/reflect/Array;", "createMultiArray", "(Ljava/lang/Class;[I)Ljava/lang/Object;") \
  V(ArrayCreateObjectArray, "Ljava/lang/reflect/Array;", "createObjectArray", "(Ljava/lang/Class;I)Ljava/lang/Object;") \
  V(ThrowableNativeFillInStackTrace, "Ljava/lang/Throwable;", "nativeFillInStackTrace", "()Ljava/lang/Object;") \
  V(UnsafeCompareAndSwapInt, "Lsun/misc/Unsafe;", "compareAndSwapInt", "(Ljava/lang/Object;JII)Z") \
  V(UnsafeGetIntVolatile, "Lsun/misc/Unsafe;", "getIntVolatile", "(Ljava/lang/Object;J)I") \
  V(UnsafePutObject, "Lsun/misc/Unsafe;", "putObject", "(Ljava/lang/Object;JLjava/lang/Object;)V") \
  V(UnsafeGetArrayBaseOffsetForComponentType, "Lsun/misc/Unsafe;", "getArrayBaseOffsetForComponentType", "(Ljava/lang/Class;)I") \
  V(UnsafeGetArrayIndexScaleForComponentType, "Lsun/misc/Unsafe;", "getArrayIndexScaleForComponentType", "(Ljava/lang/Class;)I")

#endif  // ART_RUNTIME_INTERPRETER_UNSTARTED_RUNTIME_LIST_H_
