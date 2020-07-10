/*
 * Copyright (C) 2016 The Android Open Source Project
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

/**
 * Regression tests for loop optimizations.
 */
public class Main {

  private static native void ensureJitCompiled(Class<?> cls, String methodName);

  /// CHECK-START: int Main.earlyExitFirst(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.earlyExitFirst(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int earlyExitFirst(int m) {
    int k = 0;
    for (int i = 0; i < 10; i++) {
      if (i == m) {
        return k;
      }
      k++;
    }
    return k;
  }

  /// CHECK-START: int Main.earlyExitLast(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.earlyExitLast(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int earlyExitLast(int m) {
    int k = 0;
    for (int i = 0; i < 10; i++) {
      k++;
      if (i == m) {
        return k;
      }
    }
    return k;
  }

  /// CHECK-START: int Main.earlyExitNested() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop1>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: Phi loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-START: int Main.earlyExitNested() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop1>>      outer_loop:none
  //
  /// CHECK-START: int Main.earlyExitNested() loop_optimization (after)
  /// CHECK-NOT: Phi loop:{{B\d+}} outer_loop:{{B\d+}}
  static int earlyExitNested() {
    int offset = 0;
    for (int i = 0; i < 2; i++) {
      int start = offset;
      // This loop can be removed.
      for (int j = 0; j < 2; j++) {
        offset++;
      }
      if (i == 1) {
        return start;
      }
    }
    return 0;
  }

  // Regression test for b/33774618: transfer operations involving
  // narrowing linear induction should be done correctly.
  //
  /// CHECK-START: int Main.transferNarrowWrap() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.transferNarrowWrap() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int transferNarrowWrap() {
    short x = 0;
    int w = 10;
    int v = 3;
    for (int i = 0; i < 10; i++) {
      v = w + 1;    // transfer on wrap-around
      w = x;   // wrap-around
      x += 2;  // narrowing linear
    }
    return v;
  }

  // Regression test for b/33774618: transfer operations involving
  // narrowing linear induction should be done correctly
  // (currently rejected, could be improved).
  //
  /// CHECK-START: int Main.polynomialShort() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.polynomialShort() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int polynomialShort() {
    int x = 0;
    for (short i = 0; i < 10; i++) {
      x = x - i;  // polynomial on narrowing linear
    }
    return x;
  }

  // Regression test for b/33774618: transfer operations involving
  // narrowing linear induction should be done correctly
  // (currently rejected, could be improved).
  //
  /// CHECK-START: int Main.polynomialIntFromLong() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.polynomialIntFromLong() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int polynomialIntFromLong() {
    int x = 0;
    for (long i = 0; i < 10; i++) {
      x = x - (int) i;  // polynomial on narrowing linear
    }
    return x;
  }

  /// CHECK-START: int Main.polynomialInt() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.polynomialInt() loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: int Main.polynomialInt() instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Int:i\d+>>  IntConstant -45  loop:none
  /// CHECK-DAG:               Return [<<Int>>] loop:none
  static int polynomialInt() {
    int x = 0;
    for (int i = 0; i < 10; i++) {
      x = x - i;
    }
    return x;
  }

  // Regression test for b/34779592 (found with fuzz testing): overflow for last value
  // of division truncates to zero, for multiplication it simply truncates.
  //
  /// CHECK-START: int Main.geoIntDivLastValue(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.geoIntDivLastValue(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: int Main.geoIntDivLastValue(int) instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Int:i\d+>> IntConstant 0    loop:none
  /// CHECK-DAG:              Return [<<Int>>] loop:none
  static int geoIntDivLastValue(int x) {
    for (int i = 0; i < 2; i++) {
      x /= 1081788608;
    }
    return x;
  }

  /// CHECK-START: int Main.geoIntMulLastValue(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.geoIntMulLastValue(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: int Main.geoIntMulLastValue(int) instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Par:i\d+>> ParameterValue         loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant -194211840 loop:none
  /// CHECK-DAG: <<Mul:i\d+>> Mul [<<Par>>,<<Int>>]  loop:none
  /// CHECK-DAG:              Return [<<Mul>>]       loop:none
  static int geoIntMulLastValue(int x) {
    for (int i = 0; i < 2; i++) {
      x *= 1081788608;
    }
    return x;
  }

  /// CHECK-START: long Main.geoLongDivLastValue(long) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: long Main.geoLongDivLastValue(long) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: long Main.geoLongDivLastValue(long) instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Long:j\d+>> LongConstant 0    loop:none
  /// CHECK-DAG:               Return [<<Long>>] loop:none
  //
  // Tests overflow in the divisor (while updating intermediate result).
  static long geoLongDivLastValue(long x) {
    for (int i = 0; i < 10; i++) {
      x /= 1081788608;
    }
    return x;
  }

  /// CHECK-START: long Main.geoLongDivLastValue() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: long Main.geoLongDivLastValue() loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: long Main.geoLongDivLastValue() instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Long:j\d+>> LongConstant 0    loop:none
  /// CHECK-DAG:               Return [<<Long>>] loop:none
  //
  // Tests overflow in the divisor (while updating base).
  static long geoLongDivLastValue() {
    long x = -1;
    for (int i2 = 0; i2 < 2; i2++) {
      x /= (Long.MAX_VALUE);
    }
    return x;
  }

  /// CHECK-START: long Main.geoLongMulLastValue(long) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: long Main.geoLongMulLastValue(long) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: long Main.geoLongMulLastValue(long) instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Par:j\d+>>  ParameterValue                    loop:none
  /// CHECK-DAG: <<Long:j\d+>> LongConstant -8070450532247928832 loop:none
  /// CHECK-DAG: <<Mul:j\d+>>  Mul [<<Par>>,<<Long>>]            loop:none
  /// CHECK-DAG:               Return [<<Mul>>]                  loop:none
  static long geoLongMulLastValue(long x) {
    for (int i = 0; i < 10; i++) {
      x *= 1081788608;
    }
    return x;
  }

  // If vectorized, the narrowing subscript should not cause
  // type inconsistencies in the synthesized code.
  static void narrowingSubscript(float[] a) {
    float val = 2.0f;
    for (long i = 0; i < a.length; i++) {
      a[(int) i] += val;
    }
  }

  // If vectorized, invariant stride should be recognized
  // as a reduction, not a unit stride in outer loop.
  static void reduc(int[] xx, int[] yy) {
    for (int i0 = 0; i0 < 2; i0++) {
      for (int i1 = 0; i1 < 469; i1++) {
        xx[i0] -= (++yy[i1]);
      }
    }
  }

  /// CHECK-START: void Main.string2Bytes(char[], java.lang.String) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM: void Main.string2Bytes(char[], java.lang.String) loop_optimization (after)
  /// CHECK-NOT: VecLoad
  //
  /// CHECK-START-ARM64: void Main.string2Bytes(char[], java.lang.String) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  //
  // NOTE: should correctly deal with compressed and uncompressed cases.
  private static void string2Bytes(char[] a, String b) {
    int min = Math.min(a.length, b.length());
    for (int i = 0; i < min; i++) {
      a[i] = b.charAt(i);
    }
  }

  /// CHECK-START-ARM: void Main.$noinline$stringToShorts(short[], java.lang.String) loop_optimization (after)
  /// CHECK-NOT: VecLoad

  /// CHECK-START-ARM64: void Main.$noinline$stringToShorts(short[], java.lang.String) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  private static void $noinline$stringToShorts(short[] dest, String src) {
    int min = Math.min(dest.length, src.length());
    for (int i = 0; i < min; ++i) {
      dest[i] = (short) src.charAt(i);
    }
  }

  // A strange function that does not inline.
  private static void $noinline$foo(boolean x, int n) {
    if (n < 0)
      throw new Error("oh no");
    if (n > 100) {
      $noinline$foo(!x, n - 1);
      $noinline$foo(!x, n - 2);
      $noinline$foo(!x, n - 3);
      $noinline$foo(!x, n - 4);
    }
  }

  // A loop with environment uses of x (the terminating condition). As exposed by bug
  // b/37247891, the loop can be unrolled, but should handle the (unlikely, but clearly
  // not impossible) environment uses of the terminating condition in a correct manner.
  private static void envUsesInCond() {
    boolean x = false;
    for (int i = 0; !(x = i >= 1); i++) {
      $noinline$foo(true, i);
    }
  }

  /// CHECK-START: void Main.oneBoth(short[], char[]) loop_optimization (before)
  /// CHECK-DAG: <<One:i\d+>>  IntConstant 1                       loop:none
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<One>>] loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<One>>] loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM: void Main.oneBoth(short[], char[]) loop_optimization (after)
  /// CHECK-DAG: <<One:i\d+>>  IntConstant 1                             loop:none
  /// CHECK-DAG: <<Repl:d\d+>> VecReplicateScalar [<<One>>]              loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi:i\d+>>,<<Repl>>] loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Repl>>]      loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.oneBoth(short[], char[]) loop_optimization (after)
  /// CHECK-DAG: <<One:i\d+>>  IntConstant 1                             loop:none
  /// CHECK-DAG: <<Repl:d\d+>> VecReplicateScalar [<<One>>]              loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi:i\d+>>,<<Repl>>] loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Repl>>]      loop:<<Loop>>      outer_loop:none
  //
  // Bug b/37764324: integral same-length packed types can be mixed freely.
  private static void oneBoth(short[] a, char[] b) {
    for (int i = 0; i < Math.min(a.length, b.length); i++) {
      a[i] = 1;
      b[i] = 1;
    }
  }

  // Bug b/37768917: potential dynamic BCE vs. loop optimizations
  // case should be deal with correctly (used to DCHECK fail).
  private static void arrayInTripCount(int[] a, byte[] b, int n) {
    for (int k = 0; k < n; k++) {
      for (int i = 0, u = a[0]; i < u; i++) {
        b[i] += 2;
      }
    }
  }

  /// CHECK-START: void Main.typeConv(byte[], byte[]) loop_optimization (before)
  /// CHECK-DAG: <<One:i\d+>>  IntConstant 1                       loop:none
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get:b\d+>>  ArrayGet [{{l\d+}},<<Phi>>]         loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add:i\d+>>  Add [<<Get>>,<<One>>]               loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Cnv:b\d+>>  TypeConversion [<<Add>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Cnv>>] loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM: void Main.typeConv(byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG: <<One:i\d+>>  IntConstant 1                         loop:none
  /// CHECK-DAG: <<Repl:d\d+>> VecReplicateScalar [<<One>>]          loop:none
  /// CHECK-DAG: <<Load:d\d+>> VecLoad [{{l\d+}},<<Phi1:i\d+>>]      loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Vadd:d\d+>> VecAdd [<<Load>>,<<Repl>>]            loop:<<Loop1>>      outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi1>>,<<Vadd>>] loop:<<Loop1>>      outer_loop:none
  /// CHECK-DAG: <<Get:b\d+>>  ArrayGet [{{l\d+}},<<Phi2:i\d+>>]     loop:<<Loop2:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Add:i\d+>>  Add [<<Get>>,<<One>>]                 loop:<<Loop2>>      outer_loop:none
  /// CHECK-DAG: <<Cnv:b\d+>>  TypeConversion [<<Add>>]              loop:<<Loop2>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi2>>,<<Cnv>>]  loop:<<Loop2>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.typeConv(byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG: <<One:i\d+>>  IntConstant 1                         loop:none
  /// CHECK-DAG: <<Repl:d\d+>> VecReplicateScalar [<<One>>]          loop:none
  /// CHECK-DAG: <<Load:d\d+>> VecLoad [{{l\d+}},<<Phi1:i\d+>>]      loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Vadd:d\d+>> VecAdd [<<Load>>,<<Repl>>]            loop:<<Loop1>>      outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi1>>,<<Vadd>>] loop:<<Loop1>>      outer_loop:none
  /// CHECK-DAG: <<Get:b\d+>>  ArrayGet [{{l\d+}},<<Phi2:i\d+>>]     loop:<<Loop2:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Add:i\d+>>  Add [<<Get>>,<<One>>]                 loop:<<Loop2>>      outer_loop:none
  /// CHECK-DAG: <<Cnv:b\d+>>  TypeConversion [<<Add>>]              loop:<<Loop2>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi2>>,<<Cnv>>]  loop:<<Loop2>>      outer_loop:none
  //
  // Scalar code in cleanup loop uses correct byte type on array get and type conversion.
  private static void typeConv(byte[] a, byte[] b) {
    int len = Math.min(a.length, b.length);
    for (int i = 0; i < len; i++) {
      a[i] = (byte) (b[i] + 1);
    }
  }

  // Environment of an instruction, removed during SimplifyInduction, should be adjusted.
  //
  /// CHECK-START: void Main.inductionMax(int[]) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: void Main.inductionMax(int[]) loop_optimization (after)
  /// CHECK-NOT: Phi
  private static void inductionMax(int[] a) {
   int s = 0;
    for (int i = 0; i < 10; i++) {
      s = Math.max(s, 5);
    }
  }

  /// CHECK-START: int Main.feedsIntoDeopt(int[]) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop1>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop2:B\d+>> outer_loop:none
  //
  /// CHECK-EVAL: "<<Loop1>>" != "<<Loop2>>"
  //
  /// CHECK-START: int Main.feedsIntoDeopt(int[]) loop_optimization (after)
  /// CHECK-DAG: Phi loop:{{B\d+}} outer_loop:none
  /// CHECK-NOT: Phi
  static int feedsIntoDeopt(int[] a) {
    // Reduction should be removed.
    int r = 0;
    for (int i = 0; i < 100; i++) {
      r += 10;
    }
    // Even though uses feed into deopts of BCE.
    for (int i = 1; i < 100; i++) {
      a[i] = a[i - 1];
    }
    return r;
  }

  static int absCanBeNegative(int x) {
    int a[] = { 1, 2, 3 };
    int y = 0;
    for (int i = Math.abs(x); i < a.length; i++) {
      y += a[i];
    }
    return y;
  }

  // b/65478356: sum up 2-dim array.
  static int sum(int[][] a) {
    int sum = 0;
    for (int y = 0; y < a.length; y++) {
      int[] aa = a[y];
      for (int x = 0; x < aa.length; x++) {
        sum += aa[x];
      }
    }
    return sum;
  }

  // Large loop body should not break unrolling computation.
  static void largeBody(int[] x) {
    for (int i = 0; i < 100; i++) {
      x[i] = x[i] * 1 + x[i] * 2 + x[i] * 3 + x[i] * 4 + x[i] * 5 + x[i] * 6 +
          x[i] * 7 + x[i] * 8 + x[i] * 9 + x[i] * 10 + x[i] * 11 + x[i] * 12 +
          x[i] * 13 + x[i] * 14 + x[i] * 15 + x[i] * 1 + x[i] * 2 + x[i] * 3 + x[i] * 4 +
          x[i] * 5 + x[i] * 6 + x[i] * 7 + x[i] * 8 + x[i] * 9 + x[i] * 10 + x[i] * 11 +
          x[i] * 12 + x[i] * 13 + x[i] * 14 + x[i] * 15 + x[i] * 1 + x[i] * 2 + x[i] * 3 +
          x[i] * 4 + x[i] * 5;
    }
  }

  // Mixed of 16-bit and 8-bit array references.
  static void castAndNarrow(byte[] x, char[] y) {
    for (int i = 0; i < x.length; i++) {
      x[i] = (byte) ((short) y[i] +  1);
    }
  }

  // Avoid bad scheduler-SIMD interaction.
  static int doNotMoveSIMD() {
    int sum = 0;
    for (int j = 0; j <= 8; j++) {
      int[] a = new int[17];    // a[i] = 0;
                                // ConstructorFence ?
      for (int i = 0; i < a.length; i++) {
        a[i] += 1;              // a[i] = 1;
      }
      for (int i = 0; i < a.length; i++) {
        sum += a[i];            // expect a[i] = 1;
      }
    }
    return sum;
  }

  // Ensure spilling saves full SIMD values.
  private static final int reduction32Values(int[] a, int[] b, int[] c, int[] d) {
    int s0 = 0;
    int s1 = 0;
    int s2 = 0;
    int s3 = 0;
    int s4 = 0;
    int s5 = 0;
    int s6 = 0;
    int s7 = 0;
    int s8 = 0;
    int s9 = 0;
    int s10 = 0;
    int s11 = 0;
    int s12 = 0;
    int s13 = 0;
    int s14 = 0;
    int s15 = 0;
    int s16 = 0;
    int s17 = 0;
    int s18 = 0;
    int s19 = 0;
    int s20 = 0;
    int s21 = 0;
    int s22 = 0;
    int s23 = 0;
    int s24 = 0;
    int s25 = 0;
    int s26 = 0;
    int s27 = 0;
    int s28 = 0;
    int s29 = 0;
    int s30 = 0;
    int s31 = 0;
    for (int i = 1; i < 100; i++) {
      s0 += a[i];
      s1 += b[i];
      s2 += c[i];
      s3 += d[i];
      s4 += a[i];
      s5 += b[i];
      s6 += c[i];
      s7 += d[i];
      s8 += a[i];
      s9 += b[i];
      s10 += c[i];
      s11 += d[i];
      s12 += a[i];
      s13 += b[i];
      s14 += c[i];
      s15 += d[i];
      s16 += a[i];
      s17 += b[i];
      s18 += c[i];
      s19 += d[i];
      s20 += a[i];
      s21 += b[i];
      s22 += c[i];
      s23 += d[i];
      s24 += a[i];
      s25 += b[i];
      s26 += c[i];
      s27 += d[i];
      s28 += a[i];
      s29 += b[i];
      s30 += c[i];
      s31 += d[i];
    }
    return s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8 + s9 + s10 + s11 + s12 + s13 + s14 + s15 +
           s16 + s17 + s18 + s19 + s20 + s21 + s22 + s23 +
           s24 + s25 + s26 + s27 + s28 + s29 + s30 + s31;
  }

  // Ensure spilling saves regular FP values correctly when the graph HasSIMD()
  // is true.
  /// CHECK-START-ARM64: float Main.$noinline$ensureSlowPathFPSpillFill(float[], float[], float[], float[], int[]) loop_optimization (after)
  //
  //  Both regular and SIMD accesses are present.
  /// CHECK-DAG: VecLoad
  /// CHECK-DAG: ArrayGet
  private static final float $noinline$ensureSlowPathFPSpillFill(float[] a,
                                                                 float[] b,
                                                                 float[] c,
                                                                 float[] d,
                                                                 int[] e) {
    // This loop should be vectorized so the graph->HasSIMD() will be true.
    // A power-of-2 number of iterations is chosen to avoid peeling/unrolling interference.
    for (int i = 0; i < 64; i++) {
      // The actual values of the array elements don't matter, just the
      // presence of a SIMD loop.
      e[i]++;
    }

    float f0 = 0;
    float f1 = 0;
    float f2 = 0;
    float f3 = 0;
    float f4 = 0;
    float f5 = 0;
    float f6 = 0;
    float f7 = 0;
    float f8 = 0;
    float f9 = 0;
    float f10 = 0;
    float f11 = 0;
    float f12 = 0;
    float f13 = 0;
    float f14 = 0;
    float f15 = 0;
    float f16 = 0;
    float f17 = 0;
    float f18 = 0;
    float f19 = 0;
    float f20 = 0;
    float f21 = 0;
    float f22 = 0;
    float f23 = 0;
    float f24 = 0;
    float f25 = 0;
    float f26 = 0;
    float f27 = 0;
    float f28 = 0;
    float f29 = 0;
    float f30 = 0;
    float f31 = 0;
    for (int i = 0; i < 100; i++) {
      f0 += a[i];
      f1 += b[i];
      f2 += c[i];
      f3 += d[i];
      f4 += a[i];
      f5 += b[i];
      f6 += c[i];
      f7 += d[i];
      f8 += a[i];
      f9 += b[i];
      f10 += c[i];
      f11 += d[i];
      f12 += a[i];
      f13 += b[i];
      f14 += c[i];
      f15 += d[i];
      f16 += a[i];
      f17 += b[i];
      f18 += c[i];
      f19 += d[i];
      f20 += a[i];
      f21 += b[i];
      f22 += c[i];
      f23 += d[i];
      f24 += a[i];
      f25 += b[i];
      f26 += c[i];
      f27 += d[i];
      f28 += a[i];
      f29 += b[i];
      f30 += c[i];
      f31 += d[i];
    }
    return f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8 + f9 + f10 + f11 + f12 + f13 + f14 + f15 +
           f16 + f17 + f18 + f19 + f20 + f21 + f22 + f23 +
           f24 + f25 + f26 + f27 + f28 + f29 + f30 + f31;
  }

  public static int reductionIntoReplication() {
    int[] a = { 1, 2, 3, 4 };
    int x = 0;
    for (int i = 0; i < 4; i++) {
      x += a[i];
    }
    for (int i = 0; i < 4; i++) {
      a[i] = x;
    }
    return a[3];
  }

  // Dot product and SAD vectorization idioms used to have a bug when some
  // instruction in the loop was visited twice causing a compiler crash.
  // It happened when two vectorization idioms' matched patterns had a common
  // sub-expression.

  // Idioms common sub-expression bug: DotProduct and ArraySet.
  //
  /// CHECK-START-ARM64: int Main.testDotProdAndSet(byte[], byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG:       VecDotProd
  /// CHECK-DAG:       VecStore
  public static final int testDotProdAndSet(byte[] a, byte[] b, byte[] c) {
    int s = 1;
    for (int i = 0; i < b.length; i++) {
      int temp = a[i] * b[i];
      c[i]= (byte)temp;
      s += temp;
    }
    return s - 1;
  }

  // Idioms common sub-expression bug: DotProduct and DotProduct.
  //
  /// CHECK-START-ARM64: int Main.testDotProdAndDotProd(byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG:       VecDotProd
  /// CHECK-DAG:       VecDotProd
  public static final int testDotProdAndDotProd(byte[] a, byte[] b) {
    int s0 = 1;
    int s1 = 1;
    for (int i = 0; i < b.length; i++) {
      int temp = a[i] * b[i];
      s0 += temp;
      s1 += temp;
    }
    return s0 + s1;
  }

  // Idioms common sub-expression bug: SAD and ArraySet.
  //
  /// CHECK-START-{ARM,ARM64}: int Main.testSADAndSet(int[], int[], int[]) loop_optimization (after)
  /// CHECK-DAG:       VecSADAccumulate
  /// CHECK-DAG:       VecStore
  public static int testSADAndSet(int[] x, int[] y, int[] z) {
    int min_length = Math.min(x.length, y.length);
    int sad = 0;
    for (int i = 0; i < min_length; i++) {
      int temp = Math.abs(x[i] - y[i]);
      z[i] = temp;
      sad += temp;
    }
    return sad;
  }

  // Idioms common sub-expression bug: SAD and SAD.
  /// CHECK-START-{ARM,ARM64}: int Main.testSADAndSAD(int[], int[]) loop_optimization (after)
  /// CHECK-DAG:       VecSADAccumulate
  /// CHECK-DAG:       VecSADAccumulate
  public static final int testSADAndSAD(int[] x, int[] y) {
    int s0 = 1;
    int s1 = 1;
    for (int i = 0; i < x.length; i++) {
      int temp = Math.abs(x[i] - y[i]);
      s0 += temp;
      s1 += temp;
    }
    return s0 + s1;
  }

  // Idioms common sub-expression bug: DotProd and DotProd with extra mul.
  //
  /// CHECK-START-ARM64: int Main.testDotProdAndDotProdExtraMul0(byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG:       VecMul
  /// CHECK-DAG:       VecDotProd
  /// CHECK-DAG:       VecDotProd
  public static final int testDotProdAndDotProdExtraMul0(byte[] a, byte[] b) {
    int s0 = 1;
    int s1 = 1;
    for (int i = 0; i < b.length; i++) {
      int temp0 = a[i] * b[i];
      int temp1 = (byte)(temp0) * a[i];
      s0 += temp1;
      s1 += temp0;
    }
    return s0 + s1;
  }

  // Idioms common sub-expression bug: DotProd and DotProd with extra mul (reversed order).
  //
  /// CHECK-START-ARM64: int Main.testDotProdAndDotProdExtraMul1(byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG:       VecMul
  /// CHECK-DAG:       VecDotProd
  /// CHECK-DAG:       VecDotProd
  public static final int testDotProdAndDotProdExtraMul1(byte[] a, byte[] b) {
    int s0 = 1;
    int s1 = 1;
    for (int i = 0; i < b.length; i++) {
      int temp0 = a[i] * b[i];
      int temp1 = (byte)(temp0) * a[i];
      s0 += temp0;
      s1 += temp1;
    }
    return s0 + s1;
  }

  // Idioms common sub-expression bug: SAD and SAD with extra abs.
  //
  /// CHECK-START-{ARM,ARM64}: int Main.testSADAndSADExtraAbs0(int[], int[]) loop_optimization (after)
  /// CHECK-DAG:       VecSub
  /// CHECK-DAG:       VecAbs
  /// CHECK-DAG:       VecSADAccumulate
  /// CHECK-DAG:       VecSADAccumulate
  public static final int testSADAndSADExtraAbs0(int[] x, int[] y) {
    int s0 = 1;
    int s1 = 1;
    for (int i = 0; i < x.length; i++) {
      int temp0 = Math.abs(x[i] - y[i]);
      int temp1 = Math.abs(temp0 - y[i]);
      s0 += temp1;
      s1 += temp0;
    }
    return s0 + s1;
  }

  // Idioms common sub-expression bug: SAD and SAD with extra abs (reversed order).
  //
  /// CHECK-START-{ARM,ARM64}: int Main.testSADAndSADExtraAbs1(int[], int[]) loop_optimization (after)
  /// CHECK-DAG:       VecSub
  /// CHECK-DAG:       VecAbs
  /// CHECK-DAG:       VecSADAccumulate
  /// CHECK-DAG:       VecSADAccumulate
  public static final int testSADAndSADExtraAbs1(int[] x, int[] y) {
    int s0 = 1;
    int s1 = 1;
    for (int i = 0; i < x.length; i++) {
      int temp0 = Math.abs(x[i] - y[i]);
      int temp1 = Math.abs(temp0 - y[i]);
      s0 += temp0;
      s1 += temp1;
    }
    return s0 + s1;
  }


  // Idioms common sub-expression bug: SAD and DotProd combined.
  //
  /// CHECK-START-ARM64: int Main.testSADAndDotProdCombined0(byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG:       VecSub
  /// CHECK-DAG:       VecSADAccumulate
  /// CHECK-DAG:       VecDotProd
  public static final int testSADAndDotProdCombined0(byte[] x, byte[] y) {
    int s0 = 1;
    int s1 = 1;
    for (int i = 0; i < x.length; i++) {
      int temp0 = x[i] - y[i];
      int temp1 = Math.abs(temp0);
      int temp2 = x[i] * (byte)(temp0);

      s0 += temp1;
      s1 += temp2;
    }
    return s0 + s1;
  }

  // Idioms common sub-expression bug: SAD and DotProd combined (reversed order).
  /// CHECK-START-ARM64: int Main.testSADAndDotProdCombined1(byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG:       VecSub
  /// CHECK-DAG:       VecSADAccumulate
  /// CHECK-DAG:       VecDotProd
  public static final int testSADAndDotProdCombined1(byte[] x, byte[] y) {
    int s0 = 1;
    int s1 = 1;
    for (int i = 0; i < x.length; i++) {
      int temp0 = x[i] - y[i];
      int temp1 = Math.abs(temp0);
      int temp2 = x[i] * (byte)(temp0);

      s0 += temp2;
      s1 += temp1;
    }
    return s0 + s1;
  }

  public static final int ARRAY_SIZE = 512;

  private static byte[] createAndInitByteArray(int x) {
    byte[] a = new byte[ARRAY_SIZE];
    for (int i = 0; i < a.length; i++) {
      a[i] = (byte)((~i) + x);
    }
    return a;
  }

  private static int[] createAndInitIntArray(int x) {
    int[] a = new int[ARRAY_SIZE];
    for (int i = 0; i < a.length; i++) {
      a[i] = (~i) + x;
    }
    return a;
  }

  public static void main(String[] args) {
    System.loadLibrary(args[0]);

    expectEquals(10, earlyExitFirst(-1));
    for (int i = 0; i <= 10; i++) {
      expectEquals(i, earlyExitFirst(i));
    }
    expectEquals(10, earlyExitFirst(11));

    expectEquals(10, earlyExitLast(-1));
    for (int i = 0; i < 10; i++) {
      expectEquals(i + 1, earlyExitLast(i));
    }
    expectEquals(10, earlyExitLast(10));
    expectEquals(10, earlyExitLast(11));

    expectEquals(2, earlyExitNested());

    expectEquals(17, transferNarrowWrap());
    expectEquals(-45, polynomialShort());
    expectEquals(-45, polynomialIntFromLong());
    expectEquals(-45, polynomialInt());

    expectEquals(0, geoIntDivLastValue(0));
    expectEquals(0, geoIntDivLastValue(1));
    expectEquals(0, geoIntDivLastValue(2));
    expectEquals(0, geoIntDivLastValue(1081788608));
    expectEquals(0, geoIntDivLastValue(-1081788608));
    expectEquals(0, geoIntDivLastValue(2147483647));
    expectEquals(0, geoIntDivLastValue(-2147483648));

    expectEquals(          0, geoIntMulLastValue(0));
    expectEquals( -194211840, geoIntMulLastValue(1));
    expectEquals( -388423680, geoIntMulLastValue(2));
    expectEquals(-1041498112, geoIntMulLastValue(1081788608));
    expectEquals( 1041498112, geoIntMulLastValue(-1081788608));
    expectEquals(  194211840, geoIntMulLastValue(2147483647));
    expectEquals(          0, geoIntMulLastValue(-2147483648));

    expectEquals(0L, geoLongDivLastValue(0L));
    expectEquals(0L, geoLongDivLastValue(1L));
    expectEquals(0L, geoLongDivLastValue(2L));
    expectEquals(0L, geoLongDivLastValue(1081788608L));
    expectEquals(0L, geoLongDivLastValue(-1081788608L));
    expectEquals(0L, geoLongDivLastValue(2147483647L));
    expectEquals(0L, geoLongDivLastValue(-2147483648L));
    expectEquals(0L, geoLongDivLastValue(9223372036854775807L));
    expectEquals(0L, geoLongDivLastValue(-9223372036854775808L));

    expectEquals(0L, geoLongDivLastValue());

    expectEquals(                   0L, geoLongMulLastValue(0L));
    expectEquals(-8070450532247928832L, geoLongMulLastValue(1L));
    expectEquals( 2305843009213693952L, geoLongMulLastValue(2L));
    expectEquals(                   0L, geoLongMulLastValue(1081788608L));
    expectEquals(                   0L, geoLongMulLastValue(-1081788608L));
    expectEquals( 8070450532247928832L, geoLongMulLastValue(2147483647L));
    expectEquals(                   0L, geoLongMulLastValue(-2147483648L));
    expectEquals( 8070450532247928832L, geoLongMulLastValue(9223372036854775807L));
    expectEquals(                   0L, geoLongMulLastValue(-9223372036854775808L));

    float[] a = new float[16];
    narrowingSubscript(a);
    for (int i = 0; i < 16; i++) {
      expectEquals(2.0f, a[i]);
    }

    int[] xx = new int[2];
    int[] yy = new int[469];
    reduc(xx, yy);
    expectEquals(-469, xx[0]);
    expectEquals(-938, xx[1]);
    for (int i = 0; i < 469; i++) {
      expectEquals(2, yy[i]);
    }

    char[] aa = new char[23];
    String bb = "hello world how are you";
    string2Bytes(aa, bb);
    for (int i = 0; i < aa.length; i++) {
      expectEquals(aa[i], bb.charAt(i));
    }
    String cc = "\u1010\u2020llo world how are y\u3030\u4040";
    string2Bytes(aa, cc);
    for (int i = 0; i < aa.length; i++) {
      expectEquals(aa[i], cc.charAt(i));
    }

    short[] s2s = new short[12];
    $noinline$stringToShorts(s2s, "abcdefghijkl");
    for (int i = 0; i < s2s.length; ++i) {
      expectEquals((short) "abcdefghijkl".charAt(i), s2s[i]);
    }

    envUsesInCond();

    short[] dd = new short[23];
    oneBoth(dd, aa);
    for (int i = 0; i < aa.length; i++) {
      expectEquals(aa[i], 1);
      expectEquals(dd[i], 1);
    }

    xx[0] = 10;
    byte[] bt = new byte[10];
    arrayInTripCount(xx, bt, 20);
    for (int i = 0; i < bt.length; i++) {
      expectEquals(40, bt[i]);
    }

    byte[] b1 = new byte[259];  // few extra iterations
    byte[] b2 = new byte[259];
    for (int i = 0; i < 259; i++) {
      b1[i] = 0;
      b2[i] = (byte) i;
    }
    typeConv(b1, b2);
    for (int i = 0; i < 259; i++) {
      expectEquals((byte)(i + 1), b1[i]);
    }

    inductionMax(yy);

    int[] f = new int[100];
    f[0] = 11;
    expectEquals(1000, feedsIntoDeopt(f));
    for (int i = 0; i < 100; i++) {
      expectEquals(11, f[i]);
    }

    expectEquals(0, absCanBeNegative(-3));
    expectEquals(3, absCanBeNegative(-2));
    expectEquals(5, absCanBeNegative(-1));
    expectEquals(6, absCanBeNegative(0));
    expectEquals(5, absCanBeNegative(1));
    expectEquals(3, absCanBeNegative(2));
    expectEquals(0, absCanBeNegative(3));
    expectEquals(0, absCanBeNegative(Integer.MAX_VALUE));
    // Abs(min_int) = min_int.
    int verify = 0;
    try {
      absCanBeNegative(Integer.MIN_VALUE);
      verify = 1;
    } catch (ArrayIndexOutOfBoundsException e) {
      verify = 2;
    }
    expectEquals(2, verify);

    int[][] x = new int[128][128];
    for (int i = 0; i < 128; i++) {
      for (int j = 0; j < 128; j++) {
        x[i][j] = -i - j;
      }
    }
    expectEquals(-2080768, sum(x));

    largeBody(f);
    for (int i = 0; i < 100; i++) {
      expectEquals(2805, f[i]);
    }

    char[] cx = new char[259];
    for (int i = 0; i < 259; i++) {
      cx[i] = (char) (i - 100);
    }
    castAndNarrow(b1, cx);
    for (int i = 0; i < 259; i++) {
      expectEquals((byte)((short) cx[i] + 1), b1[i]);
    }

    expectEquals(153, doNotMoveSIMD());

    // This test exposed SIMDization issues on x86 and x86_64
    // so we make sure the test runs with JIT enabled.
    ensureJitCompiled(Main.class, "reduction32Values");
    {
      int[] a1 = new int[100];
      int[] a2 = new int[100];
      int[] a3 = new int[100];
      int[] a4 = new int[100];
      for (int i = 0; i < 100; i++) {
        a1[i] = i;
        a2[i] = 1;
        a3[i] = 100 - i;
        a4[i] = i % 16;
      }
      expectEquals(85800, reduction32Values(a1, a2, a3, a4));
    }
    {
      float[] a1 = new float[100];
      float[] a2 = new float[100];
      float[] a3 = new float[100];
      float[] a4 = new float[100];
      int[] a5 = new int[100];

      for (int i = 0; i < 100; i++) {
        a1[i] = (float)i;
        a2[i] = (float)1;
        a3[i] = (float)(100 - i);
        a4[i] = (i % 16);
      }
      expectEquals(86608.0f, $noinline$ensureSlowPathFPSpillFill(a1, a2, a3, a4, a5));
    }

    expectEquals(10, reductionIntoReplication());

    {
        byte[] b_a = createAndInitByteArray(1);
        byte[] b_b = createAndInitByteArray(2);
        byte[] b_c = createAndInitByteArray(3);
        expectEquals(2731008, testDotProdAndSet(b_a, b_b, b_c));
    }
    {
        byte[] b_a = createAndInitByteArray(1);
        byte[] b_b = createAndInitByteArray(2);
        expectEquals(5462018, testDotProdAndDotProd(b_a, b_b));
    }
    {
        int[] i_a = createAndInitIntArray(1);
        int[] i_b = createAndInitIntArray(2);
        int[] i_c = createAndInitIntArray(3);
        expectEquals(512, testSADAndSet(i_a, i_b, i_c));
    }
    {
        int[] i_a = createAndInitIntArray(1);
        int[] i_b = createAndInitIntArray(2);
        expectEquals(1026, testSADAndSAD(i_a, i_b));
    }
    {
        byte[] b_a = createAndInitByteArray(1);
        byte[] b_b = createAndInitByteArray(2);
        expectEquals(2731266, testDotProdAndDotProdExtraMul0(b_a, b_b));
    }
    {
        byte[] b_a = createAndInitByteArray(1);
        byte[] b_b = createAndInitByteArray(2);
        expectEquals(2731266, testDotProdAndDotProdExtraMul1(b_a, b_b));
    }
    {
        int[] i_a = createAndInitIntArray(1);
        int[] i_b = createAndInitIntArray(2);
        expectEquals(131330, testSADAndSADExtraAbs0(i_a, i_b));
    }
    {
        int[] i_a = createAndInitIntArray(1);
        int[] i_b = createAndInitIntArray(2);
        expectEquals(131330, testSADAndSADExtraAbs1(i_a, i_b));
    }
    {
        byte[] b_a = createAndInitByteArray(1);
        byte[] b_b = createAndInitByteArray(2);
        expectEquals(1278, testSADAndDotProdCombined0(b_a, b_b));
    }
    {
        byte[] b_a = createAndInitByteArray(1);
        byte[] b_b = createAndInitByteArray(2);
        expectEquals(1278, testSADAndDotProdCombined1(b_a, b_b));
    }

    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectEquals(long expected, long result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectEquals(float expected, float result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
