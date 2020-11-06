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

#ifndef ART_RUNTIME_INTRINSICS_LIST_H_
#define ART_RUNTIME_INTRINSICS_LIST_H_

// This file defines the set of intrinsics that are supported by ART
// in the compiler and runtime. Neither compiler nor runtime has
// intrinsics for all methods here.
//
// The entries in the INTRINSICS_LIST below have the following format:
//
//   1. name
//   2. invocation-type (art::InvokeType value).
//   3. needs-environment (art::IntrinsicNeedsEnvironmentOrCache value)
//   4. side-effects (art::IntrinsicSideEffects value)
//   5. exception-info (art::::IntrinsicExceptions value)
//   6. declaring class descriptor
//   7. method name
//   8. method descriptor
//
// The needs-environment, side-effects and exception-info are compiler
// related properties (compiler/optimizing/nodes.h) that should not be
// used outside of the compiler.
//
// Note: adding a new intrinsic requires an art image version change,
// as the modifiers flag for some ArtMethods will need to be changed.
//
// Note: j.l.Integer.valueOf says kNoThrow even though it could throw an
// OOME. The kNoThrow should be renamed to kNoVisibleThrow, as it is ok to
// GVN Integer.valueOf (kNoSideEffects), and it is also OK to remove it if
// it's unused.
//
// Note: Thread.interrupted is marked with kAllSideEffects due to the lack
// of finer grain side effects representation.

// Intrinsics for methods with signature polymorphic behaviours.
#define SIGNATURE_POLYMORPHIC_INTRINSICS_LIST(V) \
  V(MethodHandleInvokeExact, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/MethodHandle;", "invokeExact", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(MethodHandleInvoke, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/MethodHandle;", "invoke", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleCompareAndExchange, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "compareAndExchange", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleCompareAndExchangeAcquire, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "compareAndExchangeAcquire", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleCompareAndExchangeRelease, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "compareAndExchangeRelease", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleCompareAndSet, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "compareAndSet", "([Ljava/lang/Object;)Z") \
  V(VarHandleGet, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "get", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAcquire, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAcquire", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndAdd, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndAdd", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndAddAcquire, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndAddAcquire", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndAddRelease, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndAddRelease", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseAnd, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseAnd", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseAndAcquire, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseAndAcquire", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseAndRelease, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseAndRelease", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseOr, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseOr", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseOrAcquire, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseOrAcquire", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseOrRelease, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseOrRelease", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseXor, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseXor", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseXorAcquire, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseXorAcquire", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndBitwiseXorRelease, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndBitwiseXorRelease", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndSet, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndSet", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndSetAcquire, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndSetAcquire", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetAndSetRelease, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getAndSetRelease", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetOpaque, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getOpaque", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleGetVolatile, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "getVolatile", "([Ljava/lang/Object;)Ljava/lang/Object;") \
  V(VarHandleSet, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "set", "([Ljava/lang/Object;)V") \
  V(VarHandleSetOpaque, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "setOpaque", "([Ljava/lang/Object;)V") \
  V(VarHandleSetRelease, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "setRelease", "([Ljava/lang/Object;)V") \
  V(VarHandleSetVolatile, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "setVolatile", "([Ljava/lang/Object;)V") \
  V(VarHandleWeakCompareAndSet, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "weakCompareAndSet", "([Ljava/lang/Object;)Z") \
  V(VarHandleWeakCompareAndSetAcquire, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "weakCompareAndSetAcquire", "([Ljava/lang/Object;)Z") \
  V(VarHandleWeakCompareAndSetPlain, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "weakCompareAndSetPlain", "([Ljava/lang/Object;)Z") \
  V(VarHandleWeakCompareAndSetRelease, kPolymorphic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/invoke/VarHandle;", "weakCompareAndSetRelease", "([Ljava/lang/Object;)Z")

// The complete list of intrinsics.
#define INTRINSICS_LIST(V) \
  V(DoubleDoubleToRawLongBits, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Double;", "doubleToRawLongBits", "(D)J") \
  V(DoubleDoubleToLongBits, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Double;", "doubleToLongBits", "(D)J") \
  V(DoubleIsInfinite, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Double;", "isInfinite", "(D)Z") \
  V(DoubleIsNaN, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Double;", "isNaN", "(D)Z") \
  V(DoubleLongBitsToDouble, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Double;", "longBitsToDouble", "(J)D") \
  V(FloatFloatToRawIntBits, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Float;", "floatToRawIntBits", "(F)I") \
  V(FloatFloatToIntBits, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Float;", "floatToIntBits", "(F)I") \
  V(FloatIsInfinite, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Float;", "isInfinite", "(F)Z") \
  V(FloatIsNaN, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Float;", "isNaN", "(F)Z") \
  V(FloatIntBitsToFloat, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Float;", "intBitsToFloat", "(I)F") \
  V(IntegerReverse, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "reverse", "(I)I") \
  V(IntegerReverseBytes, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "reverseBytes", "(I)I") \
  V(IntegerBitCount, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "bitCount", "(I)I") \
  V(IntegerCompare, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "compare", "(II)I") \
  V(IntegerDivideUnsigned, kStatic, kNeedsEnvironment, kNoSideEffects, kCanThrow, "Ljava/lang/Integer;", "divideUnsigned", "(II)I") \
  V(IntegerHighestOneBit, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "highestOneBit", "(I)I") \
  V(IntegerLowestOneBit, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "lowestOneBit", "(I)I") \
  V(IntegerNumberOfLeadingZeros, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I") \
  V(IntegerNumberOfTrailingZeros, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "numberOfTrailingZeros", "(I)I") \
  V(IntegerRotateRight, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "rotateRight", "(II)I") \
  V(IntegerRotateLeft, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "rotateLeft", "(II)I") \
  V(IntegerSignum, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "signum", "(I)I") \
  V(LongReverse, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "reverse", "(J)J") \
  V(LongReverseBytes, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "reverseBytes", "(J)J") \
  V(LongBitCount, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "bitCount", "(J)I") \
  V(LongCompare, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "compare", "(JJ)I") \
  V(LongDivideUnsigned, kStatic, kNeedsEnvironment, kNoSideEffects, kCanThrow, "Ljava/lang/Long;", "divideUnsigned", "(JJ)J") \
  V(LongHighestOneBit, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "highestOneBit", "(J)J") \
  V(LongLowestOneBit, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "lowestOneBit", "(J)J") \
  V(LongNumberOfLeadingZeros, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "numberOfLeadingZeros", "(J)I") \
  V(LongNumberOfTrailingZeros, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "numberOfTrailingZeros", "(J)I") \
  V(LongRotateRight, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "rotateRight", "(JI)J") \
  V(LongRotateLeft, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "rotateLeft", "(JI)J") \
  V(LongSignum, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Long;", "signum", "(J)I") \
  V(ShortReverseBytes, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Short;", "reverseBytes", "(S)S") \
  V(MathAbsDouble, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "abs", "(D)D") \
  V(MathAbsFloat, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "abs", "(F)F") \
  V(MathAbsLong, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "abs", "(J)J") \
  V(MathAbsInt, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "abs", "(I)I") \
  V(MathMinDoubleDouble, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "min", "(DD)D") \
  V(MathMinFloatFloat, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "min", "(FF)F") \
  V(MathMinLongLong, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "min", "(JJ)J") \
  V(MathMinIntInt, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "min", "(II)I") \
  V(MathMaxDoubleDouble, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "max", "(DD)D") \
  V(MathMaxFloatFloat, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "max", "(FF)F") \
  V(MathMaxLongLong, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "max", "(JJ)J") \
  V(MathMaxIntInt, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "max", "(II)I") \
  V(MathCos, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "cos", "(D)D") \
  V(MathSin, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "sin", "(D)D") \
  V(MathAcos, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "acos", "(D)D") \
  V(MathAsin, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "asin", "(D)D") \
  V(MathAtan, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "atan", "(D)D") \
  V(MathAtan2, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "atan2", "(DD)D") \
  V(MathPow, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "pow", "(DD)D") \
  V(MathCbrt, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "cbrt", "(D)D") \
  V(MathCosh, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "cosh", "(D)D") \
  V(MathExp, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "exp", "(D)D") \
  V(MathExpm1, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "expm1", "(D)D") \
  V(MathHypot, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "hypot", "(DD)D") \
  V(MathLog, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "log", "(D)D") \
  V(MathLog10, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "log10", "(D)D") \
  V(MathNextAfter, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "nextAfter", "(DD)D") \
  V(MathSinh, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "sinh", "(D)D") \
  V(MathTan, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "tan", "(D)D") \
  V(MathTanh, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "tanh", "(D)D") \
  V(MathSqrt, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "sqrt", "(D)D") \
  V(MathCeil, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "ceil", "(D)D") \
  V(MathFloor, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "floor", "(D)D") \
  V(MathRint, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "rint", "(D)D") \
  V(MathRoundDouble, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "round", "(D)J") \
  V(MathRoundFloat, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Math;", "round", "(F)I") \
  V(SystemArrayCopyChar, kStatic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/System;", "arraycopy", "([CI[CII)V") \
  V(SystemArrayCopy, kStatic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/System;", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V") \
  V(ThreadCurrentThread, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;") \
  V(MemoryPeekByte, kStatic, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Llibcore/io/Memory;", "peekByte", "(J)B") \
  V(MemoryPeekIntNative, kStatic, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Llibcore/io/Memory;", "peekIntNative", "(J)I") \
  V(MemoryPeekLongNative, kStatic, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Llibcore/io/Memory;", "peekLongNative", "(J)J") \
  V(MemoryPeekShortNative, kStatic, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Llibcore/io/Memory;", "peekShortNative", "(J)S") \
  V(MemoryPokeByte, kStatic, kNeedsEnvironment, kWriteSideEffects, kCanThrow, "Llibcore/io/Memory;", "pokeByte", "(JB)V") \
  V(MemoryPokeIntNative, kStatic, kNeedsEnvironment, kWriteSideEffects, kCanThrow, "Llibcore/io/Memory;", "pokeIntNative", "(JI)V") \
  V(MemoryPokeLongNative, kStatic, kNeedsEnvironment, kWriteSideEffects, kCanThrow, "Llibcore/io/Memory;", "pokeLongNative", "(JJ)V") \
  V(MemoryPokeShortNative, kStatic, kNeedsEnvironment, kWriteSideEffects, kCanThrow, "Llibcore/io/Memory;", "pokeShortNative", "(JS)V") \
  V(FP16Ceil, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "ceil", "(S)S") \
  V(FP16Floor, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "floor", "(S)S") \
  V(FP16Rint, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "rint", "(S)S") \
  V(FP16ToFloat, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "toFloat", "(S)F") \
  V(FP16ToHalf, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "toHalf", "(F)S") \
  V(FP16Greater, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "greater", "(SS)Z") \
  V(FP16GreaterEquals, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "greaterEquals", "(SS)Z") \
  V(FP16Less, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "less", "(SS)Z") \
  V(FP16LessEquals, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Llibcore/util/FP16;", "lessEquals", "(SS)Z") \
  V(StringCharAt, kVirtual, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Ljava/lang/String;", "charAt", "(I)C") \
  V(StringCompareTo, kVirtual, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Ljava/lang/String;", "compareTo", "(Ljava/lang/String;)I") \
  V(StringEquals, kVirtual, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Ljava/lang/String;", "equals", "(Ljava/lang/Object;)Z") \
  V(StringGetCharsNoCheck, kVirtual, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Ljava/lang/String;", "getCharsNoCheck", "(II[CI)V") \
  V(StringIndexOf, kVirtual, kNeedsEnvironment, kReadSideEffects, kNoThrow, "Ljava/lang/String;", "indexOf", "(I)I") \
  V(StringIndexOfAfter, kVirtual, kNeedsEnvironment, kReadSideEffects, kNoThrow, "Ljava/lang/String;", "indexOf", "(II)I") \
  V(StringStringIndexOf, kVirtual, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Ljava/lang/String;", "indexOf", "(Ljava/lang/String;)I") \
  V(StringStringIndexOfAfter, kVirtual, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Ljava/lang/String;", "indexOf", "(Ljava/lang/String;I)I") \
  V(StringIsEmpty, kVirtual, kNeedsEnvironment, kReadSideEffects, kNoThrow, "Ljava/lang/String;", "isEmpty", "()Z") \
  V(StringLength, kVirtual, kNeedsEnvironment, kReadSideEffects, kNoThrow, "Ljava/lang/String;", "length", "()I") \
  V(StringNewStringFromBytes, kStatic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringFactory;", "newStringFromBytes", "([BIII)Ljava/lang/String;") \
  V(StringNewStringFromChars, kStatic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringFactory;", "newStringFromChars", "(II[C)Ljava/lang/String;") \
  V(StringNewStringFromString, kStatic, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringFactory;", "newStringFromString", "(Ljava/lang/String;)Ljava/lang/String;") \
  V(StringBufferAppend, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuffer;", "append", "(Ljava/lang/String;)Ljava/lang/StringBuffer;") \
  V(StringBufferLength, kVirtual, kNeedsEnvironment, kAllSideEffects, kNoThrow, "Ljava/lang/StringBuffer;", "length", "()I") \
  V(StringBufferToString, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuffer;", "toString", "()Ljava/lang/String;") \
  V(StringBuilderAppendObject, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(Ljava/lang/Object;)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendString, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(Ljava/lang/String;)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendCharSequence, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(Ljava/lang/CharSequence;)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendCharArray, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "([C)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendBoolean, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(Z)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendChar, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(C)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendInt, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(I)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendLong, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(J)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendFloat, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(F)Ljava/lang/StringBuilder;") \
  V(StringBuilderAppendDouble, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "append", "(D)Ljava/lang/StringBuilder;") \
  V(StringBuilderLength, kVirtual, kNeedsEnvironment, kReadSideEffects, kNoThrow, "Ljava/lang/StringBuilder;", "length", "()I") \
  V(StringBuilderToString, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/StringBuilder;", "toString", "()Ljava/lang/String;") \
  V(UnsafeCASInt, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "compareAndSwapInt", "(Ljava/lang/Object;JII)Z") \
  V(UnsafeCASLong, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "compareAndSwapLong", "(Ljava/lang/Object;JJJ)Z") \
  V(UnsafeCASObject, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "compareAndSwapObject", "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z") \
  V(UnsafeGet, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getInt", "(Ljava/lang/Object;J)I") \
  V(UnsafeGetVolatile, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getIntVolatile", "(Ljava/lang/Object;J)I") \
  V(UnsafeGetObject, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getObject", "(Ljava/lang/Object;J)Ljava/lang/Object;") \
  V(UnsafeGetObjectVolatile, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getObjectVolatile", "(Ljava/lang/Object;J)Ljava/lang/Object;") \
  V(UnsafeGetLong, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getLong", "(Ljava/lang/Object;J)J") \
  V(UnsafeGetLongVolatile, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getLongVolatile", "(Ljava/lang/Object;J)J") \
  V(UnsafePut, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putInt", "(Ljava/lang/Object;JI)V") \
  V(UnsafePutOrdered, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putOrderedInt", "(Ljava/lang/Object;JI)V") \
  V(UnsafePutVolatile, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putIntVolatile", "(Ljava/lang/Object;JI)V") \
  V(UnsafePutObject, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putObject", "(Ljava/lang/Object;JLjava/lang/Object;)V") \
  V(UnsafePutObjectOrdered, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putOrderedObject", "(Ljava/lang/Object;JLjava/lang/Object;)V") \
  V(UnsafePutObjectVolatile, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putObjectVolatile", "(Ljava/lang/Object;JLjava/lang/Object;)V") \
  V(UnsafePutLong, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putLong", "(Ljava/lang/Object;JJ)V") \
  V(UnsafePutLongOrdered, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putOrderedLong", "(Ljava/lang/Object;JJ)V") \
  V(UnsafePutLongVolatile, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "putLongVolatile", "(Ljava/lang/Object;JJ)V") \
  V(UnsafeGetAndAddInt, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getAndAddInt", "(Ljava/lang/Object;JI)I") \
  V(UnsafeGetAndAddLong, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getAndAddLong", "(Ljava/lang/Object;JJ)J") \
  V(UnsafeGetAndSetInt, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getAndSetInt", "(Ljava/lang/Object;JI)I") \
  V(UnsafeGetAndSetLong, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getAndSetLong", "(Ljava/lang/Object;JJ)J") \
  V(UnsafeGetAndSetObject, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "getAndSetObject", "(Ljava/lang/Object;JLjava/lang/Object;)Ljava/lang/Object;") \
  V(UnsafeLoadFence, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "loadFence", "()V") \
  V(UnsafeStoreFence, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "storeFence", "()V") \
  V(UnsafeFullFence, kVirtual, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Lsun/misc/Unsafe;", "fullFence", "()V") \
  V(ReferenceGetReferent, kDirect, kNeedsEnvironment, kAllSideEffects, kCanThrow, "Ljava/lang/ref/Reference;", "getReferent", "()Ljava/lang/Object;") \
  V(IntegerValueOf, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/lang/Integer;", "valueOf", "(I)Ljava/lang/Integer;") \
  V(ThreadInterrupted, kStatic, kNeedsEnvironment, kAllSideEffects, kNoThrow, "Ljava/lang/Thread;", "interrupted", "()Z") \
  V(VarHandleFullFence, kStatic, kNeedsEnvironment, kWriteSideEffects, kNoThrow, "Ljava/lang/invoke/VarHandle;", "fullFence", "()V") \
  V(VarHandleAcquireFence, kStatic, kNeedsEnvironment, kWriteSideEffects, kNoThrow, "Ljava/lang/invoke/VarHandle;", "acquireFence", "()V") \
  V(VarHandleReleaseFence, kStatic, kNeedsEnvironment, kWriteSideEffects, kNoThrow, "Ljava/lang/invoke/VarHandle;", "releaseFence", "()V") \
  V(VarHandleLoadLoadFence, kStatic, kNeedsEnvironment, kWriteSideEffects, kNoThrow, "Ljava/lang/invoke/VarHandle;", "loadLoadFence", "()V") \
  V(VarHandleStoreStoreFence, kStatic, kNeedsEnvironment, kReadSideEffects, kNoThrow, "Ljava/lang/invoke/VarHandle;", "storeStoreFence", "()V") \
  V(ReachabilityFence, kStatic, kNeedsEnvironment, kWriteSideEffects, kNoThrow, "Ljava/lang/ref/Reference;", "reachabilityFence", "(Ljava/lang/Object;)V") \
  V(CRC32Update, kStatic, kNeedsEnvironment, kNoSideEffects, kNoThrow, "Ljava/util/zip/CRC32;", "update", "(II)I") \
  V(CRC32UpdateBytes, kStatic, kNeedsEnvironment, kReadSideEffects, kCanThrow, "Ljava/util/zip/CRC32;", "updateBytes", "(I[BII)I") \
  V(CRC32UpdateByteBuffer, kStatic, kNeedsEnvironment, kReadSideEffects, kNoThrow, "Ljava/util/zip/CRC32;", "updateByteBuffer", "(IJII)I") \
  SIGNATURE_POLYMORPHIC_INTRINSICS_LIST(V)

#endif  // ART_RUNTIME_INTRINSICS_LIST_H_
#undef ART_RUNTIME_INTRINSICS_LIST_H_   // #define is only for lint.
