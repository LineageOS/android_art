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

public class RemTest {
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
    remInt();
    remLong();
  }

  private static void remInt() {
    expectEquals(0, $noinline$IntRemBy18(0));
    expectEquals(1, $noinline$IntRemBy18(1));
    expectEquals(-1, $noinline$IntRemBy18(-1));
    expectEquals(0, $noinline$IntRemBy18(18));
    expectEquals(0, $noinline$IntRemBy18(-18));
    expectEquals(11, $noinline$IntRemBy18(65));
    expectEquals(-11, $noinline$IntRemBy18(-65));

    expectEquals(0, $noinline$IntRemByMinus18(0));
    expectEquals(1, $noinline$IntRemByMinus18(1));
    expectEquals(-1, $noinline$IntRemByMinus18(-1));
    expectEquals(0, $noinline$IntRemByMinus18(18));
    expectEquals(0, $noinline$IntRemByMinus18(-18));
    expectEquals(11, $noinline$IntRemByMinus18(65));
    expectEquals(-11, $noinline$IntRemByMinus18(-65));

    expectEquals(0, $noinline$IntRemBy7(0));
    expectEquals(1, $noinline$IntRemBy7(1));
    expectEquals(-1, $noinline$IntRemBy7(-1));
    expectEquals(0, $noinline$IntRemBy7(7));
    expectEquals(0, $noinline$IntRemBy7(-7));
    expectEquals(1, $noinline$IntRemBy7(22));
    expectEquals(-1, $noinline$IntRemBy7(-22));

    expectEquals(0, $noinline$IntRemByMinus7(0));
    expectEquals(1, $noinline$IntRemByMinus7(1));
    expectEquals(-1, $noinline$IntRemByMinus7(-1));
    expectEquals(0, $noinline$IntRemByMinus7(7));
    expectEquals(0, $noinline$IntRemByMinus7(-7));
    expectEquals(1, $noinline$IntRemByMinus7(22));
    expectEquals(-1, $noinline$IntRemByMinus7(-22));

    expectEquals(0, $noinline$IntRemBy6(0));
    expectEquals(1, $noinline$IntRemBy6(1));
    expectEquals(-1, $noinline$IntRemBy6(-1));
    expectEquals(0, $noinline$IntRemBy6(6));
    expectEquals(0, $noinline$IntRemBy6(-6));
    expectEquals(1, $noinline$IntRemBy6(19));
    expectEquals(-1, $noinline$IntRemBy6(-19));

    expectEquals(0, $noinline$IntRemByMinus6(0));
    expectEquals(1, $noinline$IntRemByMinus6(1));
    expectEquals(-1, $noinline$IntRemByMinus6(-1));
    expectEquals(0, $noinline$IntRemByMinus6(6));
    expectEquals(0, $noinline$IntRemByMinus6(-6));
    expectEquals(1, $noinline$IntRemByMinus6(19));
    expectEquals(-1, $noinline$IntRemByMinus6(-19));
  }

  // A test case to check that 'lsr' and 'asr' are combined into one 'asr'.
  // For divisor 18 seen in an MP3 decoding workload there is no need
  // to correct the result of get_high(dividend * magic). So there are no
  // instructions between 'lsr' and 'asr'. In such a case they can be combined
  // into one 'asr'.
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntRemBy18(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK-NEXT:            mov w{{\d+}}, #0x12
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntRemBy18(int v) {
    int r = v % 18;
    return r;
  }

  // A test case to check that 'lsr' and 'asr' are combined into one 'asr'.
  // Divisor -18 has the same property as divisor 18: no need to correct the
  // result of get_high(dividend * magic). So there are no
  // instructions between 'lsr' and 'asr'. In such a case they can be combined
  // into one 'asr'.
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntRemByMinus18(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK-NEXT:            mov w{{\d+}}, #0xffffffee
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntRemByMinus18(int v) {
    int r = v % -18;
    return r;
  }

  // A test case to check that 'lsr' and 'add' are combined into one 'adds'.
  // For divisor 7 seen in the core library the result of get_high(dividend * magic)
  // must be corrected by the 'add' instruction.
  //
  // The test case also checks 'add' and 'add_shift' are optimized into 'adds' and 'cinc'.
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntRemBy7(int) disassembly (after)
  /// CHECK:                 adds x{{\d+}}, x{{\d+}}, x{{\d+}}, lsl #32
  /// CHECK-NEXT:            asr  x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            cinc w{{\d+}}, w{{\d+}}, mi
  /// CHECK-NEXT:            mov w{{\d+}}, #0x7
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntRemBy7(int v) {
    int r = v % 7;
    return r;
  }

  // A test case to check that 'lsr' and 'add' are combined into one 'adds'.
  // Divisor -7 has the same property as divisor 7: the result of get_high(dividend * magic)
  // must be corrected. In this case it is a 'sub' instruction.
  //
  // The test case also checks 'sub' and 'add_shift' are optimized into 'subs' and 'cinc'.
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntRemByMinus7(int) disassembly (after)
  /// CHECK:                 subs x{{\d+}}, x{{\d+}}, x{{\d+}}, lsl #32
  /// CHECK-NEXT:            asr  x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            cinc w{{\d+}}, w{{\d+}}, mi
  /// CHECK-NEXT:            mov w{{\d+}}, #0xfffffff9
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntRemByMinus7(int v) {
    int r = v % -7;
    return r;
  }

  // A test case to check that 'asr' is used to get the high 32 bits of the result of
  // 'dividend * magic'.
  // For divisor 6 seen in the core library there is no need to correct the result of
  // get_high(dividend * magic). Also there is no 'asr' before the final 'add' instruction
  // which uses only the high 32 bits of the result. In such a case 'asr' getting the high
  // 32 bits can be used as well.
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntRemBy6(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK-NEXT:            mov w{{\d+}}, #0x6
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntRemBy6(int v) {
    int r = v % 6;
    return r;
  }

  // A test case to check that 'asr' is used to get the high 32 bits of the result of
  // 'dividend * magic'.
  // Divisor -6 has the same property as divisor 6: no need to correct the result of
  // get_high(dividend * magic) and no 'asr' before the final 'add' instruction
  // which uses only the high 32 bits of the result. In such a case 'asr' getting the high
  // 32 bits can be used as well.
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntRemByMinus6(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK-NEXT:            mov w{{\d+}}, #0xfffffffa
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntRemByMinus6(int v) {
    int r = v % -6;
    return r;
  }

  private static void remLong() {
    expectEquals(0L, $noinline$LongRemBy18(0L));
    expectEquals(1L, $noinline$LongRemBy18(1L));
    expectEquals(-1L, $noinline$LongRemBy18(-1L));
    expectEquals(0L, $noinline$LongRemBy18(18L));
    expectEquals(0L, $noinline$LongRemBy18(-18L));
    expectEquals(11L, $noinline$LongRemBy18(65L));
    expectEquals(-11L, $noinline$LongRemBy18(-65L));

    expectEquals(0L, $noinline$LongRemByMinus18(0L));
    expectEquals(1L, $noinline$LongRemByMinus18(1L));
    expectEquals(-1L, $noinline$LongRemByMinus18(-1L));
    expectEquals(0L, $noinline$LongRemByMinus18(18L));
    expectEquals(0L, $noinline$LongRemByMinus18(-18L));
    expectEquals(11L, $noinline$LongRemByMinus18(65L));
    expectEquals(-11L, $noinline$LongRemByMinus18(-65L));

    expectEquals(0L, $noinline$LongRemBy7(0L));
    expectEquals(1L, $noinline$LongRemBy7(1L));
    expectEquals(-1L, $noinline$LongRemBy7(-1L));
    expectEquals(0L, $noinline$LongRemBy7(7L));
    expectEquals(0L, $noinline$LongRemBy7(-7L));
    expectEquals(1L, $noinline$LongRemBy7(22L));
    expectEquals(-1L, $noinline$LongRemBy7(-22L));

    expectEquals(0L, $noinline$LongRemByMinus7(0L));
    expectEquals(1L, $noinline$LongRemByMinus7(1L));
    expectEquals(-1L, $noinline$LongRemByMinus7(-1L));
    expectEquals(0L, $noinline$LongRemByMinus7(7L));
    expectEquals(0L, $noinline$LongRemByMinus7(-7L));
    expectEquals(1L, $noinline$LongRemByMinus7(22L));
    expectEquals(-1L, $noinline$LongRemByMinus7(-22L));

    expectEquals(0L, $noinline$LongRemBy6(0L));
    expectEquals(1L, $noinline$LongRemBy6(1L));
    expectEquals(-1L, $noinline$LongRemBy6(-1L));
    expectEquals(0L, $noinline$LongRemBy6(6L));
    expectEquals(0L, $noinline$LongRemBy6(-6L));
    expectEquals(1L, $noinline$LongRemBy6(19L));
    expectEquals(-1L, $noinline$LongRemBy6(-19L));

    expectEquals(0L, $noinline$LongRemByMinus6(0L));
    expectEquals(1L, $noinline$LongRemByMinus6(1L));
    expectEquals(-1L, $noinline$LongRemByMinus6(-1L));
    expectEquals(0L, $noinline$LongRemByMinus6(6L));
    expectEquals(0L, $noinline$LongRemByMinus6(-6L));
    expectEquals(1L, $noinline$LongRemByMinus6(19L));
    expectEquals(-1L, $noinline$LongRemByMinus6(-19L));

    expectEquals(0L, $noinline$LongRemBy100(0L));
    expectEquals(1L, $noinline$LongRemBy100(1L));
    expectEquals(-1L, $noinline$LongRemBy100(-1L));
    expectEquals(0L, $noinline$LongRemBy100(100L));
    expectEquals(0L, $noinline$LongRemBy100(-100L));
    expectEquals(1L, $noinline$LongRemBy100(101L));
    expectEquals(-1L, $noinline$LongRemBy100(-101L));

    expectEquals(0L, $noinline$LongRemByMinus100(0L));
    expectEquals(1L, $noinline$LongRemByMinus100(1L));
    expectEquals(-1L, $noinline$LongRemByMinus100(-1L));
    expectEquals(0L, $noinline$LongRemByMinus100(100L));
    expectEquals(0L, $noinline$LongRemByMinus100(-100L));
    expectEquals(1L, $noinline$LongRemByMinus100(101L));
    expectEquals(-1L, $noinline$LongRemByMinus100(-101L));
  }

  // Test cases for Int64 HDiv/HRem to check that optimizations implemented for Int32 are not
  // used for Int64. The same divisors 18, -18, 7, -7, 6 and -6 are used.

  /// CHECK-START-ARM64: long RemTest.$noinline$LongRemBy18(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x12
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$LongRemBy18(long v) {
    long r = v % 18L;
    return r;
  }

  /// CHECK-START-ARM64: long RemTest.$noinline$LongRemByMinus18(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0xffffffffffffffee
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$LongRemByMinus18(long v) {
    long r = v % -18L;
    return r;
  }

  /// CHECK-START-ARM64: long RemTest.$noinline$LongRemBy7(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            asr x{{\d+}}, x{{\d+}}, #1
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x7
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$LongRemBy7(long v) {
    long r = v % 7L;
    return r;
  }

  /// CHECK-START-ARM64: long RemTest.$noinline$LongRemByMinus7(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            asr x{{\d+}}, x{{\d+}}, #1
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0xfffffffffffffff9
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$LongRemByMinus7(long v) {
    long r = v % -7L;
    return r;
  }

  /// CHECK-START-ARM64: long RemTest.$noinline$LongRemBy6(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$LongRemBy6(long v) {
    long r = v % 6L;
    return r;
  }

  /// CHECK-START-ARM64: long RemTest.$noinline$LongRemByMinus6(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0xfffffffffffffffa
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$LongRemByMinus6(long v) {
    long r = v % -6L;
    return r;
  }

  // A test to check 'add' and 'add_shift' are optimized into 'adds' and 'cinc'.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$LongRemBy100(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            adds  x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            asr   x{{\d+}}, x{{\d+}}, #6
  /// CHECK-NEXT:            cinc  x{{\d+}}, x{{\d+}}, mi
  /// CHECK-NEXT:            mov x{{\d+}}, #0x64
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$LongRemBy100(long v) {
    long r = v % 100L;
    return r;
  }

  // A test to check 'sub' and 'add_shift' are optimized into 'subs' and 'cinc'.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$LongRemByMinus100(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            subs  x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            asr   x{{\d+}}, x{{\d+}}, #6
  /// CHECK-NEXT:            cinc  x{{\d+}}, x{{\d+}}, mi
  /// CHECK-NEXT:            mov x{{\d+}}, #0xffffffffffffff9c
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$LongRemByMinus100(long v) {
    long r = v % -100L;
    return r;
  }
}
