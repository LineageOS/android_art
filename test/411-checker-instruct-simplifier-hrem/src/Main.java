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

public class Main {
  private static void expectEquals(long expected, long result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectEquals(long[] div_rem_expected, long[] result) {
    expectEquals(div_rem_expected[0], result[0]);
    expectEquals(div_rem_expected[1], result[1]);
  }

  private static void remInt() {
    expectEquals(1L, $noinline$IntDivRemBy18(1));
    expectEquals(1L << 32 | 2L, $noinline$IntDivRemBy18(20));

    expectEquals(1L, $noinline$IntRemDivBy18(1));
    expectEquals(1L << 32 | 2L, $noinline$IntRemDivBy18(20));

    expectEquals(1L, $noinline$IntDivRemBy18(1, false));
    expectEquals(1L << 32 | 2L, $noinline$IntDivRemBy18(20, true));

    expectEquals(1L, $noinline$IntDivRemByMinus18(1));
    expectEquals(-1L, $noinline$IntDivRemBy18(-1));
    expectEquals((-1L << 32) | 2L, $noinline$IntDivRemByMinus18(20));
    expectEquals((1L << 32) | (-2L & 0x00000000ffffffff), $noinline$IntDivRemByMinus18(-20));

    expectEquals(0L, $noinline$IntDivRemBy5(0));
    expectEquals(1L, $noinline$IntDivRemBy5(1));
    expectEquals(1L << 32, $noinline$IntDivRemBy5(5));
    expectEquals((1L << 32) | 1L, $noinline$IntDivRemBy5(6));
    expectEquals((-1L << 32) | 0x00000000ffffffff, $noinline$IntDivRemBy5(-6));
    expectEquals(-1L << 32, $noinline$IntDivRemBy5(-5));
    expectEquals(0x00000000ffffffff, $noinline$IntDivRemBy5(-1));

    expectEquals(0L, $noinline$IntDivRemByMinus5(0));
    expectEquals(1L, $noinline$IntDivRemByMinus5(1));
    expectEquals(-1L << 32, $noinline$IntDivRemByMinus5(5));
    expectEquals((-1L << 32) | 1L, $noinline$IntDivRemByMinus5(6));
    expectEquals((1L << 32) | 0x00000000ffffffff, $noinline$IntDivRemByMinus5(-6));
    expectEquals(1L << 32, $noinline$IntDivRemByMinus5(-5));
    expectEquals(0x00000000ffffffff, $noinline$IntDivRemByMinus5(-1));

    expectEquals(0L, $noinline$IntDivRemBy7(0));
    expectEquals(1L, $noinline$IntDivRemBy7(1));
    expectEquals(1L << 32, $noinline$IntDivRemBy7(7));
    expectEquals((1L << 32) | 1L, $noinline$IntDivRemBy7(8));
    expectEquals((-1L << 32) | 0x00000000ffffffff, $noinline$IntDivRemBy7(-8));
    expectEquals(-1L << 32, $noinline$IntDivRemBy7(-7));
    expectEquals(0x00000000ffffffff, $noinline$IntDivRemBy7(-1));

    expectEquals(0L, $noinline$IntDivRemByMinus7(0));
    expectEquals(1L, $noinline$IntDivRemByMinus7(1));
    expectEquals(-1L << 32, $noinline$IntDivRemByMinus7(7));
    expectEquals((-1L << 32) | 1L, $noinline$IntDivRemByMinus7(8));
    expectEquals((1L << 32) | 0x00000000ffffffff, $noinline$IntDivRemByMinus7(-8));
    expectEquals(1L << 32, $noinline$IntDivRemByMinus7(-7));
    expectEquals(0x00000000ffffffff, $noinline$IntDivRemByMinus7(-1));

    expectEquals(0L, $noinline$IntDivRemByMaxInt(0));
    expectEquals(1L, $noinline$IntDivRemByMaxInt(1));
    expectEquals(1L << 32, $noinline$IntDivRemByMaxInt(Integer.MAX_VALUE));
    expectEquals(Integer.MAX_VALUE - 1, $noinline$IntDivRemByMaxInt(Integer.MAX_VALUE - 1));
    expectEquals((-1L << 32) | 0x00000000ffffffff, $noinline$IntDivRemByMaxInt(Integer.MIN_VALUE));
    expectEquals(0x00000000ffffffff, $noinline$IntDivRemByMaxInt(-1));
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRemBy18(int) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRemBy18(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long Main.$noinline$IntDivRemBy18(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK:                 msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static long $noinline$IntDivRemBy18(int v) {
    int q = v / 18;
    int r = v % 18;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRemByMinus18(int) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRemByMinus18(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long Main.$noinline$IntDivRemByMinus18(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK:                 msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static long $noinline$IntDivRemByMinus18(int v) {
    int q = v / -18;
    int r = v % -18;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntRemDivBy18(int) instruction_simplifier (before)
  /// CHECK:           Rem
  /// CHECK:           Div
  //
  /// CHECK-START: long Main.$noinline$IntRemDivBy18(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long Main.$noinline$IntRemDivBy18(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK:                 msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static long $noinline$IntRemDivBy18(int v) {
    int r = v % 18;
    int q = v / 18;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRemBy5(int) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRemBy5(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Shl
  /// CHECK-NEXT:      Add
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long Main.$noinline$IntDivRemBy5(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #33
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK:                 add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsl #2
  /// CHECK:                 sub w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static long $noinline$IntDivRemBy5(int v) {
    int q = v / 5;
    int r = v % 5;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRemByMinus5(int) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRemByMinus5(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long Main.$noinline$IntDivRemByMinus5(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #33
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK:                 msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static long $noinline$IntDivRemByMinus5(int v) {
    int q = v / -5;
    int r = v % -5;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRemBy7(int) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRemBy7(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Shl
  /// CHECK-NEXT:      Sub
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long Main.$noinline$IntDivRemBy7(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            cinc w{{\d+}}, w{{\d+}}, mi
  /// CHECK:                 lsl w{{\d+}}, w{{\d+}}, #3
  /// CHECK:                 sub w{{\d+}}, w{{\d+}}, w{{\d+}}
  /// CHECK:                 sub w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static long $noinline$IntDivRemBy7(int v) {
    int q = v / 7;
    int r = v % 7;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRemByMinus7(int) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRemByMinus7(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long Main.$noinline$IntDivRemByMinus7(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            cinc w{{\d+}}, w{{\d+}}, mi
  /// CHECK:                 msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static long $noinline$IntDivRemByMinus7(int v) {
    int q = v / -7;
    int r = v % -7;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRemByMaxInt(int) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRemByMaxInt(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Shl
  /// CHECK-NEXT:      Sub
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long Main.$noinline$IntDivRemByMaxInt(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #61
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK:                 lsl w{{\d+}}, w{{\d+}}, #31
  /// CHECK:                 sub w{{\d+}}, w{{\d+}}, w{{\d+}}
  /// CHECK:                 sub w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static long $noinline$IntDivRemByMaxInt(int v) {
    int q = v / Integer.MAX_VALUE;
    int r = v % Integer.MAX_VALUE;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  HDiv with the same inputs as HRem but in another basic block is not reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRemBy18(int, boolean) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRemBy18(int, boolean) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK:           Rem
  private static long $noinline$IntDivRemBy18(int v, boolean do_division) {
    long result = 0;
    if (do_division) {
      int q = v / 18;
      result = (long)q << 32;
    }
    int r = v % 18;
    return result | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRem(int, int) instruction_simplifier$after_gvn (before)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRem(int, int) instruction_simplifier$after_gvn (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  private static long $noinline$IntDivRem(int v, int s) {
    int q = v / s;
    int r = v % s;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$IntRemDiv(int, int) instruction_simplifier$after_gvn (before)
  /// CHECK:           Rem
  /// CHECK-NEXT:      Div
  //
  /// CHECK-START: long Main.$noinline$IntRemDiv(int, int) instruction_simplifier$after_gvn (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  private static long $noinline$IntRemDiv(int v, int s) {
    int r = v % s;
    int q = v / s;
    return ((long)q << 32) | r;
  }

  // A test case to check:
  //  HDiv with the same inputs as HRem but in another basic block is not reused.
  //
  /// CHECK-START: long Main.$noinline$IntDivRem(int, int, boolean) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$IntDivRem(int, int, boolean) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK:           Rem
  private static long $noinline$IntDivRem(int v, int s, boolean do_division) {
    long result = 0;
    if (do_division) {
      int q = v / s;
      result = (long)q << 32;
    }
    int r = v % s;
    return result | r;
  }

  // A test case to check:
  //  If HRem is in a loop, the instruction simplifier postpones its optimization till
  //  loop analysis/optimizations are done.
  //
  /// CHECK-START: int Main.$noinline$IntRemBy18InLoop(int) instruction_simplifier (before)
  /// CHECK:           Div loop:B{{\d+}}
  /// CHECK-NEXT:      Rem loop:B{{\d+}}
  //
  /// CHECK-START: int Main.$noinline$IntRemBy18InLoop(int) instruction_simplifier (after)
  /// CHECK:           Div loop:B{{\d+}}
  /// CHECK-NEXT:      Rem loop:B{{\d+}}
  //
  /// CHECK-START: int Main.$noinline$IntRemBy18InLoop(int) instruction_simplifier$after_bce (before)
  /// CHECK:           Div loop:B{{\d+}}
  /// CHECK-NEXT:      Rem loop:B{{\d+}}
  //
  /// CHECK-START: int Main.$noinline$IntRemBy18InLoop(int) instruction_simplifier$after_bce (after)
  /// CHECK-NOT:       Rem
  /// CHECK:           Div loop:B{{\d+}}
  /// CHECK-NEXT:      Mul loop:B{{\d+}}
  /// CHECK-NEXT:      Sub loop:B{{\d+}}
  private static int $noinline$IntRemBy18InLoop(int v) {
    int[] values = new int[v];
    for (int i = 0; i < values.length; ++i) {
      int q = i / 18;
      int r = i % 18;
      values[i] = q + r;
    }
    return values[v - 1];
  }

  // A test case to check:
  //  FP type HRem is not optimized by the instruction simplifier.
  //
  /// CHECK-START: float Main.$noinline$FloatRemBy18(float) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  //
  /// CHECK-START: float Main.$noinline$FloatRemBy18(float) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  private static float $noinline$FloatRemBy18(float v) {
    float q = v / 18.0f;
    float r = v % 18.0f;
    return q + r;
  }

  // A test case to check:
  //  FP type HRem is not optimized by the instruction simplifier.
  //
  /// CHECK-START: double Main.$noinline$DoubleRemBy18(double) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  //
  /// CHECK-START: double Main.$noinline$DoubleRemBy18(double) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  private static double $noinline$DoubleRemBy18(double v) {
    double q = v / 18.0;
    double r = v % 18.0;
    return q + r;
  }

  // A test case to check:
  //  HRem with a divisor of power 2 is not optimized by the instruction simplifier because
  //  the case is optimized by the code generator.
  //
  /// CHECK-START: int Main.$noinline$IntRemByIntMin(int) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  //
  /// CHECK-START: int Main.$noinline$IntRemByIntMin(int) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  private static int $noinline$IntRemByIntMin(int v) {
    int q = v / Integer.MIN_VALUE;
    int r = v % Integer.MIN_VALUE;
    return q + r;
  }

  private static void remLong() {
    expectEquals(1L, $noinline$LongDivRemBy18(1L));
    expectEquals(1L << 32 | 2L, $noinline$LongDivRemBy18(20L));

    expectEquals(1L, $noinline$LongRemDivBy18(1L));
    expectEquals(1L << 32 | 2L, $noinline$LongRemDivBy18(20L));

    expectEquals(1L, $noinline$LongDivRemBy18(1L, false));
    expectEquals(1L << 32 | 2L, $noinline$LongDivRemBy18(20L, true));

    expectEquals(new long[] {0L, 1L}, $noinline$LongDivRemByMinus18(1));
    expectEquals(new long[] {0L, -1L}, $noinline$LongDivRemByMinus18(-1));
    expectEquals(new long[] {-1L, 2L}, $noinline$LongDivRemByMinus18(20));
    expectEquals(new long[] {1L, -2L}, $noinline$LongDivRemByMinus18(-20));

    expectEquals(new long[] {0L, 0L}, $noinline$LongDivRemBy5(0));
    expectEquals(new long[] {0L, 1L}, $noinline$LongDivRemBy5(1));
    expectEquals(new long[] {1L, 0L}, $noinline$LongDivRemBy5(5));
    expectEquals(new long[] {1L, 1L}, $noinline$LongDivRemBy5(6));
    expectEquals(new long[] {-1L, -1L}, $noinline$LongDivRemBy5(-6));
    expectEquals(new long[] {-1L, 0L}, $noinline$LongDivRemBy5(-5));
    expectEquals(new long[] {0L, -1L}, $noinline$LongDivRemBy5(-1));

    expectEquals(new long[] {0L, 0L}, $noinline$LongDivRemByMinus5(0));
    expectEquals(new long[] {0L, 1L}, $noinline$LongDivRemByMinus5(1));
    expectEquals(new long[] {-1L, 0L}, $noinline$LongDivRemByMinus5(5));
    expectEquals(new long[] {-1L, 1L}, $noinline$LongDivRemByMinus5(6));
    expectEquals(new long[] {1L, -1L}, $noinline$LongDivRemByMinus5(-6));
    expectEquals(new long[] {1L, 0L}, $noinline$LongDivRemByMinus5(-5));
    expectEquals(new long[] {0L, -1L}, $noinline$LongDivRemByMinus5(-1));

    expectEquals(new long[] {0L, 0L}, $noinline$LongDivRemBy7(0));
    expectEquals(new long[] {0L, 1L}, $noinline$LongDivRemBy7(1));
    expectEquals(new long[] {1L, 0L}, $noinline$LongDivRemBy7(7));
    expectEquals(new long[] {1L, 1L}, $noinline$LongDivRemBy7(8));
    expectEquals(new long[] {-1L, -1L}, $noinline$LongDivRemBy7(-8));
    expectEquals(new long[] {-1L, 0L}, $noinline$LongDivRemBy7(-7));
    expectEquals(new long[] {0L, -1L}, $noinline$LongDivRemBy7(-1));

    expectEquals(new long[] {0L, 0L}, $noinline$LongDivRemByMinus7(0));
    expectEquals(new long[] {0L, 1L}, $noinline$LongDivRemByMinus7(1));
    expectEquals(new long[] {-1L, 0L}, $noinline$LongDivRemByMinus7(7));
    expectEquals(new long[] {-1L, 1L}, $noinline$LongDivRemByMinus7(8));
    expectEquals(new long[] {1L, -1L}, $noinline$LongDivRemByMinus7(-8));
    expectEquals(new long[] {1L, 0L}, $noinline$LongDivRemByMinus7(-7));
    expectEquals(new long[] {0L, -1L}, $noinline$LongDivRemByMinus7(-1));

    expectEquals(new long[] {0L, 0L}, $noinline$LongDivRemByMaxLong(0));
    expectEquals(new long[] {0L, 1L}, $noinline$LongDivRemByMaxLong(1));
    expectEquals(new long[] {1L, 0L}, $noinline$LongDivRemByMaxLong(Long.MAX_VALUE));
    expectEquals(new long[] {0L, Long.MAX_VALUE - 1},
                 $noinline$LongDivRemByMaxLong(Long.MAX_VALUE - 1));
    expectEquals(new long[] {-1L, -1L}, $noinline$LongDivRemByMaxLong(Long.MIN_VALUE));
    expectEquals(new long[] {0L, -1L}, $noinline$LongDivRemByMaxLong(-1));
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$LongDivRemBy18(long) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$LongDivRemBy18(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  private static long $noinline$LongDivRemBy18(long v) {
    long q = v / 18L;
    long r = v % 18L;
    return (q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemByMinus18(long) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemByMinus18(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long[] Main.$noinline$LongDivRemByMinus18(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add   x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK:                 msub  x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long[] $noinline$LongDivRemByMinus18(long v) {
    long q = v / -18L;
    long r = v % -18L;
    return new long[] {q, r};
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$LongRemDivBy18(long) instruction_simplifier (before)
  /// CHECK:           Rem
  /// CHECK:           Div
  //
  /// CHECK-START: long Main.$noinline$LongRemDivBy18(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  private static long $noinline$LongRemDivBy18(long v) {
    long r = v % 18L;
    long q = v / 18L;
    return (q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemBy5(long) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemBy5(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Shl
  /// CHECK-NEXT:      Add
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long[] Main.$noinline$LongDivRemBy5(long) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #1
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK:                 add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsl #2
  /// CHECK:                 sub x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long[] $noinline$LongDivRemBy5(long v) {
    long q = v / 5L;
    long r = v % 5L;
    return new long[] {q, r};
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemByMinus5(long) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemByMinus5(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long[] Main.$noinline$LongDivRemByMinus5(long) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #1
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK:                 msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long[] $noinline$LongDivRemByMinus5(long v) {
    long q = v / -5L;
    long r = v % -5L;
    return new long[] {q, r};
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemBy7(long) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemBy7(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Shl
  /// CHECK-NEXT:      Sub
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long[] Main.$noinline$LongDivRemBy7(long) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #1
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK:                 lsl x{{\d+}}, x{{\d+}}, #3
  /// CHECK:                 sub x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK:                 sub x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long[] $noinline$LongDivRemBy7(long v) {
    long q = v / 7L;
    long r = v % 7L;
    return new long[] {q, r};
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemByMinus7(long) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemByMinus7(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long[] Main.$noinline$LongDivRemByMinus7(long) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #1
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK:                 msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long[] $noinline$LongDivRemByMinus7(long v) {
    long q = v / -7L;
    long r = v % -7L;
    return new long[] {q, r};
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemByMaxLong(long) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long[] Main.$noinline$LongDivRemByMaxLong(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Shl
  /// CHECK-NEXT:      Sub
  /// CHECK-NEXT:      Sub
  //
  /// CHECK-START-ARM64: long[] Main.$noinline$LongDivRemByMaxLong(long) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #61
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK:                 lsl x{{\d+}}, x{{\d+}}, #63
  /// CHECK:                 sub x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK:                 sub x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long[] $noinline$LongDivRemByMaxLong(long v) {
    long q = v / Long.MAX_VALUE;
    long r = v % Long.MAX_VALUE;
    return new long[] {q, r};
  }

  // A test case to check:
  //  HDiv with the same inputs as HRem but in another basic block is not reused.
  //
  /// CHECK-START: long Main.$noinline$LongDivRemBy18(long, boolean) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$LongDivRemBy18(long, boolean) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK:           Rem
  private static long $noinline$LongDivRemBy18(long v, boolean do_division) {
    long result = 0;
    if (do_division) {
      long q = v / 18L;
      result = q << 32;
    }
    long r = v % 18L;
    return result | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$LongDivRem(long, long) instruction_simplifier$after_gvn (before)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  //
  /// CHECK-START: long Main.$noinline$LongDivRem(long, long) instruction_simplifier$after_gvn (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  private static long $noinline$LongDivRem(long v, long s) {
    long q = v / s;
    long r = v % s;
    return (q << 32) | r;
  }

  // A test case to check:
  //  If there is HDiv with the same inputs as HRem, it is reused.
  //
  /// CHECK-START: long Main.$noinline$LongRemDiv(long, long) instruction_simplifier$after_gvn (before)
  /// CHECK:           Rem
  /// CHECK-NEXT:      Div
  //
  /// CHECK-START: long Main.$noinline$LongRemDiv(long, long) instruction_simplifier$after_gvn (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Mul
  /// CHECK-NEXT:      Sub
  private static long $noinline$LongRemDiv(long v, long s) {
    long r = v % s;
    long q = v / s;
    return (q << 32) | r;
  }

  // A test case to check:
  //  HDiv with the same inputs as HRem but in another basic block is not reused.
  //
  /// CHECK-START: long Main.$noinline$LongDivRem(long, long, boolean) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK:           Rem
  //
  /// CHECK-START: long Main.$noinline$LongDivRem(long, long, boolean) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK:           Rem
  private static long $noinline$LongDivRem(long v, long s, boolean do_division) {
    long result = 0;
    if (do_division) {
      long q = v / s;
      result = q << 32;
    }
    long r = v % s;
    return result | r;
  }

  // A test case to check:
  //  If HRem is in a loop, the instruction simplifier postpones its optimization till
  //  loop analysis/optimizations are done.
  //
  /// CHECK-START: long Main.$noinline$LongRemBy18InLoop(long) instruction_simplifier (before)
  /// CHECK:           Div loop:B{{\d+}}
  /// CHECK-NEXT:      Rem loop:B{{\d+}}
  //
  /// CHECK-START: long Main.$noinline$LongRemBy18InLoop(long) instruction_simplifier (after)
  /// CHECK:           Div loop:B{{\d+}}
  /// CHECK-NEXT:      Rem loop:B{{\d+}}
  //
  /// CHECK-START: long Main.$noinline$LongRemBy18InLoop(long) instruction_simplifier$after_bce (before)
  /// CHECK:           Div loop:B{{\d+}}
  /// CHECK-NEXT:      Rem loop:B{{\d+}}
  //
  /// CHECK-START: long Main.$noinline$LongRemBy18InLoop(long) instruction_simplifier$after_bce (after)
  /// CHECK-NOT:       Rem
  /// CHECK:           Div loop:B{{\d+}}
  /// CHECK-NEXT:      Mul loop:B{{\d+}}
  /// CHECK-NEXT:      Sub loop:B{{\d+}}
  private static long $noinline$LongRemBy18InLoop(long v) {
    long[] values = new long[(int)v];
    for (int i = 0; i < values.length; ++i) {
      long d = (long)i;
      long q = d / 18L;
      long r = d % 18L;
      values[i] = q + r;
    }
    return values[values.length - 1];
  }

  // A test case to check:
  //  HRem with a divisor of power 2 is not optimized by the instruction simplifier because
  //  the case is optimized by the code generator.
  //
  /// CHECK-START: long Main.$noinline$LongRemByLongMin(long) instruction_simplifier (before)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  //
  /// CHECK-START: long Main.$noinline$LongRemByLongMin(long) instruction_simplifier (after)
  /// CHECK:           Div
  /// CHECK-NEXT:      Rem
  private static long $noinline$LongRemByLongMin(long v) {
    long q = v / Long.MIN_VALUE;
    long r = v % Long.MIN_VALUE;
    return q + r;
  }

  public static void main(String args[]) {
    remInt();
    remLong();
  }
}
