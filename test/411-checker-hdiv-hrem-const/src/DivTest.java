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

public class DivTest {
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

  public static void main() {
    divInt();
    divLong();
  }

  private static void divInt() {
    expectEquals(0, $noinline$IntDivBy18(0));
    expectEquals(0, $noinline$IntDivBy18(1));
    expectEquals(0, $noinline$IntDivBy18(-1));
    expectEquals(1, $noinline$IntDivBy18(18));
    expectEquals(-1, $noinline$IntDivBy18(-18));
    expectEquals(3, $noinline$IntDivBy18(65));
    expectEquals(-3, $noinline$IntDivBy18(-65));

    expectEquals(0, $noinline$IntDivByMinus18(0));
    expectEquals(0, $noinline$IntDivByMinus18(1));
    expectEquals(0, $noinline$IntDivByMinus18(-1));
    expectEquals(-1, $noinline$IntDivByMinus18(18));
    expectEquals(1, $noinline$IntDivByMinus18(-18));
    expectEquals(-3, $noinline$IntDivByMinus18(65));
    expectEquals(3, $noinline$IntDivByMinus18(-65));

    expectEquals(0, $noinline$IntDivBy7(0));
    expectEquals(0, $noinline$IntDivBy7(1));
    expectEquals(0, $noinline$IntDivBy7(-1));
    expectEquals(1, $noinline$IntDivBy7(7));
    expectEquals(-1, $noinline$IntDivBy7(-7));
    expectEquals(3, $noinline$IntDivBy7(22));
    expectEquals(-3, $noinline$IntDivBy7(-22));

    expectEquals(0, $noinline$IntDivByMinus7(0));
    expectEquals(0, $noinline$IntDivByMinus7(1));
    expectEquals(0, $noinline$IntDivByMinus7(-1));
    expectEquals(-1, $noinline$IntDivByMinus7(7));
    expectEquals(1, $noinline$IntDivByMinus7(-7));
    expectEquals(-3, $noinline$IntDivByMinus7(22));
    expectEquals(3, $noinline$IntDivByMinus7(-22));

    expectEquals(0, $noinline$IntDivBy6(0));
    expectEquals(0, $noinline$IntDivBy6(1));
    expectEquals(0, $noinline$IntDivBy6(-1));
    expectEquals(1, $noinline$IntDivBy6(6));
    expectEquals(-1, $noinline$IntDivBy6(-6));
    expectEquals(3, $noinline$IntDivBy6(19));
    expectEquals(-3, $noinline$IntDivBy6(-19));

    expectEquals(0, $noinline$IntDivByMinus6(0));
    expectEquals(0, $noinline$IntDivByMinus6(1));
    expectEquals(0, $noinline$IntDivByMinus6(-1));
    expectEquals(-1, $noinline$IntDivByMinus6(6));
    expectEquals(1, $noinline$IntDivByMinus6(-6));
    expectEquals(-3, $noinline$IntDivByMinus6(19));
    expectEquals(3, $noinline$IntDivByMinus6(-19));
  }

  // A test case to check that 'lsr' and 'asr' are combined into one 'asr'.
  // For divisor 18 seen in an MP3 decoding workload there is no need
  // to correct the result of get_high(dividend * magic). So there are no
  // instructions between 'lsr' and 'asr'. In such a case they can be combined
  // into one 'asr'.
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntDivBy18(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$IntDivBy18(int v) {
    int r = v / 18;
    return r;
  }

  // A test case to check that 'lsr' and 'asr' are combined into one 'asr'.
  // Divisor -18 has the same property as divisor 18: no need to correct the
  // result of get_high(dividend * magic). So there are no
  // instructions between 'lsr' and 'asr'. In such a case they can be combined
  // into one 'asr'.
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntDivByMinus18(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$IntDivByMinus18(int v) {
    int r = v / -18;
    return r;
  }

  // A test case to check that 'lsr' and 'add' are combined into one 'adds'.
  // For divisor 7 seen in the core library the result of get_high(dividend * magic)
  // must be corrected by the 'add' instruction.
  //
  // The test case also checks 'add' and 'add_shift' are optimized into 'adds' and 'cinc'.
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntDivBy7(int) disassembly (after)
  /// CHECK:                 adds x{{\d+}}, x{{\d+}}, x{{\d+}}, lsl #32
  /// CHECK-NEXT:            asr  x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            cinc w{{\d+}}, w{{\d+}}, mi
  private static int $noinline$IntDivBy7(int v) {
    int r = v / 7;
    return r;
  }

  // A test case to check that 'lsr' and 'add' are combined into one 'adds'.
  // Divisor -7 has the same property as divisor 7: the result of get_high(dividend * magic)
  // must be corrected. In this case it is a 'sub' instruction.
  //
  // The test case also checks 'sub' and 'add_shift' are optimized into 'subs' and 'cinc'.
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntDivByMinus7(int) disassembly (after)
  /// CHECK:                 subs x{{\d+}}, x{{\d+}}, x{{\d+}}, lsl #32
  /// CHECK-NEXT:            asr  x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            cinc w{{\d+}}, w{{\d+}}, mi
  private static int $noinline$IntDivByMinus7(int v) {
    int r = v / -7;
    return r;
  }

  // A test case to check that 'asr' is used to get the high 32 bits of the result of
  // 'dividend * magic'.
  // For divisor 6 seen in the core library there is no need to correct the result of
  // get_high(dividend * magic). Also there is no 'asr' before the final 'add' instruction
  // which uses only the high 32 bits of the result. In such a case 'asr' getting the high
  // 32 bits can be used as well.
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntDivBy6(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$IntDivBy6(int v) {
    int r = v / 6;
    return r;
  }

  // A test case to check that 'asr' is used to get the high 32 bits of the result of
  // 'dividend * magic'.
  // Divisor -6 has the same property as divisor 6: no need to correct the result of
  // get_high(dividend * magic) and no 'asr' before the final 'add' instruction
  // which uses only the high 32 bits of the result. In such a case 'asr' getting the high
  // 32 bits can be used as well.
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntDivByMinus6(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$IntDivByMinus6(int v) {
    int r = v / -6;
    return r;
  }

  private static void divLong() {
    expectEquals(0L, $noinline$LongDivBy18(0L));
    expectEquals(0L, $noinline$LongDivBy18(1L));
    expectEquals(0L, $noinline$LongDivBy18(-1L));
    expectEquals(1L, $noinline$LongDivBy18(18L));
    expectEquals(-1L, $noinline$LongDivBy18(-18L));
    expectEquals(3L, $noinline$LongDivBy18(65L));
    expectEquals(-3L, $noinline$LongDivBy18(-65L));

    expectEquals(0L, $noinline$LongDivByMinus18(0L));
    expectEquals(0L, $noinline$LongDivByMinus18(1L));
    expectEquals(0L, $noinline$LongDivByMinus18(-1L));
    expectEquals(-1L, $noinline$LongDivByMinus18(18L));
    expectEquals(1L, $noinline$LongDivByMinus18(-18L));
    expectEquals(-3L, $noinline$LongDivByMinus18(65L));
    expectEquals(3L, $noinline$LongDivByMinus18(-65L));

    expectEquals(0L, $noinline$LongDivBy7(0L));
    expectEquals(0L, $noinline$LongDivBy7(1L));
    expectEquals(0L, $noinline$LongDivBy7(-1L));
    expectEquals(1L, $noinline$LongDivBy7(7L));
    expectEquals(-1L, $noinline$LongDivBy7(-7L));
    expectEquals(3L, $noinline$LongDivBy7(22L));
    expectEquals(-3L, $noinline$LongDivBy7(-22L));

    expectEquals(0L, $noinline$LongDivByMinus7(0L));
    expectEquals(0L, $noinline$LongDivByMinus7(1L));
    expectEquals(0L, $noinline$LongDivByMinus7(-1L));
    expectEquals(-1L, $noinline$LongDivByMinus7(7L));
    expectEquals(1L, $noinline$LongDivByMinus7(-7L));
    expectEquals(-3L, $noinline$LongDivByMinus7(22L));
    expectEquals(3L, $noinline$LongDivByMinus7(-22L));

    expectEquals(0L, $noinline$LongDivBy6(0L));
    expectEquals(0L, $noinline$LongDivBy6(1L));
    expectEquals(0L, $noinline$LongDivBy6(-1L));
    expectEquals(1L, $noinline$LongDivBy6(6L));
    expectEquals(-1L, $noinline$LongDivBy6(-6L));
    expectEquals(3L, $noinline$LongDivBy6(19L));
    expectEquals(-3L, $noinline$LongDivBy6(-19L));

    expectEquals(0L, $noinline$LongDivByMinus6(0L));
    expectEquals(0L, $noinline$LongDivByMinus6(1L));
    expectEquals(0L, $noinline$LongDivByMinus6(-1L));
    expectEquals(-1L, $noinline$LongDivByMinus6(6L));
    expectEquals(1L, $noinline$LongDivByMinus6(-6L));
    expectEquals(-3L, $noinline$LongDivByMinus6(19L));
    expectEquals(3L, $noinline$LongDivByMinus6(-19L));

    expectEquals(0L, $noinline$LongDivBy100(0L));
    expectEquals(0L, $noinline$LongDivBy100(1L));
    expectEquals(0L, $noinline$LongDivBy100(-1L));
    expectEquals(1L, $noinline$LongDivBy100(100L));
    expectEquals(-1L, $noinline$LongDivBy100(-100L));
    expectEquals(3L, $noinline$LongDivBy100(301L));
    expectEquals(-3L, $noinline$LongDivBy100(-301L));

    expectEquals(0L, $noinline$LongDivByMinus100(0L));
    expectEquals(0L, $noinline$LongDivByMinus100(1L));
    expectEquals(0L, $noinline$LongDivByMinus100(-1L));
    expectEquals(-1L, $noinline$LongDivByMinus100(100L));
    expectEquals(1L, $noinline$LongDivByMinus100(-100L));
    expectEquals(-3L, $noinline$LongDivByMinus100(301L));
    expectEquals(3L, $noinline$LongDivByMinus100(-301L));
  }

  // Test cases for Int64 HDiv/HRem to check that optimizations implemented for Int32 are not
  // used for Int64. The same divisors 18, -18, 7, -7, 6 and -6 are used.

  /// CHECK-START-ARM64: long DivTest.$noinline$LongDivBy18(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$LongDivBy18(long v) {
    long r = v / 18L;
    return r;
  }

  /// CHECK-START-ARM64: long DivTest.$noinline$LongDivByMinus18(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$LongDivByMinus18(long v) {
    long r = v / -18L;
    return r;
  }

  /// CHECK-START-ARM64: long DivTest.$noinline$LongDivBy7(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            asr x{{\d+}}, x{{\d+}}, #1
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$LongDivBy7(long v) {
    long r = v / 7L;
    return r;
  }

  /// CHECK-START-ARM64: long DivTest.$noinline$LongDivByMinus7(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            asr x{{\d+}}, x{{\d+}}, #1
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$LongDivByMinus7(long v) {
    long r = v / -7L;
    return r;
  }

  /// CHECK-START-ARM64: long DivTest.$noinline$LongDivBy6(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$LongDivBy6(long v) {
    long r = v / 6L;
    return r;
  }

  /// CHECK-START-ARM64: long DivTest.$noinline$LongDivByMinus6(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$LongDivByMinus6(long v) {
    long r = v / -6L;
    return r;
  }

  // A test to check 'add' and 'add_shift' are optimized into 'adds' and 'cinc'.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$LongDivBy100(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            adds  x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            asr   x{{\d+}}, x{{\d+}}, #6
  /// CHECK-NEXT:            cinc  x{{\d+}}, x{{\d+}}, mi
  private static long $noinline$LongDivBy100(long v) {
    long r = v / 100L;
    return r;
  }

  // A test to check 'subs' and 'add_shift' are optimized into 'subs' and 'cinc'.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$LongDivByMinus100(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            subs  x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            asr   x{{\d+}}, x{{\d+}}, #6
  /// CHECK-NEXT:            cinc  x{{\d+}}, x{{\d+}}, mi
  private static long $noinline$LongDivByMinus100(long v) {
    long r = v / -100L;
    return r;
  }
}
