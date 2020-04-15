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

public class HaddOther {
  private static final int N = 2 * 1024;
  private static final int M = N + 31;

  //  Should be just shift right, not halving add.
  //
  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_short2short(short[], short[]) loop_optimization (after)
  /// CHECK: VecShr

  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_short2short(short[], short[]) loop_optimization (after)
  /// CHECK-NOT: VecHalvingAdd
  private static void test_no_hadd_short2short(short[] a, short[] out) {
    int min_length = Math.min(out.length, a.length);
    for (int i = 0; i < min_length; i++) {
      out[i] = (short) (a[i] >> 1);
    }
  }

  //  This loop is not vectorized: shift right with a signed type is not supported.
  //
  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_short2short_logical(short[], short[]) loop_optimization (after)
  /// CHECK-NOT: VecLoad
  /// CHECK-NOT: VecHalvingAdd
  private static void test_no_hadd_short2short_logical(short[] a, short[] out) {
    int min_length = Math.min(out.length, a.length);
    for (int i = 0; i < min_length; i++) {
      out[i] = (short) (a[i] >>> 1);
    }
  }

  //  This loop is not vectorized: mismatched packed type size.
  //
  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_int2short(int[], short[]) loop_optimization (after)
  /// CHECK-NOT: VecLoad
  /// CHECK-NOT: VecHalvingAdd
  private static void test_no_hadd_int2short(int[] a, short[] out) {
    int min_length = Math.min(out.length, a.length);
    for (int i = 0; i < min_length; i++) {
      out[i] = (short) (a[i] >> 1);
    }
  }

  //  Should be just shift right, not halving add.
  //
  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_int2int(int[], int[]) loop_optimization (after)
  /// CHECK: VecShr

  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_int2int(int[], int[]) loop_optimization (after)
  /// CHECK-NOT: VecHalvingAdd
  private static void test_no_hadd_int2int(int[] a, int[] out) {
    int min_length = Math.min(out.length, a.length);
    for (int i = 0; i < min_length; i++) {
      out[i] = a[i] >> 1;
    }
  }

  //  Should be just add and shift right, not halving add.
  //
  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_sum_casted(short[], short[], short[]) loop_optimization (after)
  /// CHECK: VecAdd
  /// CHECK: VecShr

  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_sum_casted(short[], short[], short[]) loop_optimization (after)
  /// CHECK-NOT: VecHalvingAdd
  private static void test_no_hadd_sum_casted(short[] a, short[] b, short[] out) {
    int min_length = Math.min(out.length, Math.min(a.length, b.length));
    for (int i = 0; i < min_length; i++) {
      out[i] = (short) (((short) (a[i] + b[i])) >> 1);
    }
  }

  //  This loop is not vectorized: mismatched packed type size.
  //
  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_sum_casted_ints(int[], int[], int[]) loop_optimization (after)
  /// CHECK-NOT: VecLoad
  /// CHECK-NOT: VecHalvingAdd
  private static void test_no_hadd_sum_casted_ints(int[] a, int[] b, int[] out) {
    int min_length = Math.min(out.length, Math.min(a.length, b.length));
    for (int i = 0; i < min_length; i++) {
      out[i] = (short) ((short) (a[i] + b[i]) >> 1);
    }
  }

  //  Should be an add, followed by a halving add.
  //
  /// CHECK-START-{ARM,ARM64}: void HaddOther.test_no_hadd_sum_casted_plus_const(short[], short[], short[]) loop_optimization (after)
  /// CHECK: VecAdd
  /// CHECK: VecHalvingAdd
  private static void test_no_hadd_sum_casted_plus_const(short[] a, short[] b, short[] out) {
    int min_length = Math.min(out.length, Math.min(a.length, b.length));
    for (int i = 0; i < min_length; i++) {
      out[i] = (short) (((short) (a[i] + b[i]) + 1) >> 1);
    }
  }

  public static void main() {
    short[] sA = new short[M];
    short[] sB = new short[M];
    short[] sOut = new short[M];
    int[] iA = new int[M];
    int[] iB = new int[M];
    int[] iOut = new int[M];

    // Some interesting values.
    short[] interesting = {
      (short) 0x0000,
      (short) 0x0001,
      (short) 0x0002,
      (short) 0x1234,
      (short) 0x8000,
      (short) 0x8001,
      (short) 0x7fff,
      (short) 0xffff
    };
    // Initialize cross-values to test all cases, and also
    // set up some extra values to exercise the cleanup loop.
    for (int i = 0; i < M; i++) {
      sA[i] = (short) i;
      sB[i] = interesting[i & 7];
      iA[i] = i;
      iB[i] = interesting[i & 7];
    }

    test_no_hadd_short2short(sA, sOut);
    for (int i = 0; i < M; i++) {
      short e = (short) (sA[i] >> 1);
      expectEquals(e, sOut[i]);
    }
    test_no_hadd_short2short_logical(sA, sOut);
    for (int i = 0; i < M; i++) {
      short e = (short) (sA[i] >>> 1);
      expectEquals(e, sOut[i]);
    }
    test_no_hadd_int2short(iA, sOut);
    for (int i = 0; i < M; i++) {
      short e = (short) (iA[i] >> 1);
      expectEquals(e, sOut[i]);
    }
    test_no_hadd_int2int(iA, iOut);
    for (int i = 0; i < M; i++) {
      int e = iA[i] >> 1;
      expectEquals(e, iOut[i]);
    }
    test_no_hadd_sum_casted(sA, sB, sOut);
    for (int i = 0; i < M; i++) {
      short e = (short) (((short) (sA[i] + sB[i])) >> 1);
      expectEquals(e, sOut[i]);
    }
    test_no_hadd_sum_casted_ints(iA, iB, iOut);
    for (int i = 0; i < M; i++) {
      int e = (short) ((short) (iA[i] + iB[i]) >> 1);
      expectEquals(e, iOut[i]);
    }
    test_no_hadd_sum_casted_plus_const(sA, sB, sOut);
    for (int i = 0; i < M; i++) {
      short e = (short) (((short) (sA[i] + sB[i]) + 1) >> 1);
      expectEquals(e, sOut[i]);
    }

    System.out.println("HaddOther passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
