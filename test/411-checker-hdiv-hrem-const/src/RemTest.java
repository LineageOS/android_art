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

    expectEquals(0, $noinline$IntALenRemBy18(new int[0]));
    expectEquals(1, $noinline$IntALenRemBy18(new int[1]));
    expectEquals(0, $noinline$IntALenRemBy18(new int[18]));
    expectEquals(11, $noinline$IntALenRemBy18(new int[65]));

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

    expectEquals(0, $noinline$IntALenRemBy7(new int[0]));
    expectEquals(1, $noinline$IntALenRemBy7(new int[1]));
    expectEquals(0, $noinline$IntALenRemBy7(new int[7]));
    expectEquals(1, $noinline$IntALenRemBy7(new int[22]));

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

    expectEquals(0, $noinline$IntALenRemBy6(new int[0]));
    expectEquals(1, $noinline$IntALenRemBy6(new int[1]));
    expectEquals(0, $noinline$IntALenRemBy6(new int[6]));
    expectEquals(1, $noinline$IntALenRemBy6(new int[19]));

    expectEquals(0, $noinline$IntRemByMinus6(0));
    expectEquals(1, $noinline$IntRemByMinus6(1));
    expectEquals(-1, $noinline$IntRemByMinus6(-1));
    expectEquals(0, $noinline$IntRemByMinus6(6));
    expectEquals(0, $noinline$IntRemByMinus6(-6));
    expectEquals(1, $noinline$IntRemByMinus6(19));
    expectEquals(-1, $noinline$IntRemByMinus6(-19));

    expectEquals(1, $noinline$UnsignedIntRem01(13));
    expectEquals(1, $noinline$UnsignedIntRem02(13));
    expectEquals(1, $noinline$UnsignedIntRem03(13));
    expectEquals(1, $noinline$UnsignedIntRem04(13));
    expectEquals(1, $noinline$UnsignedIntRem05(101));
    expectEquals(11, $noinline$UnsignedIntRem06(101));

    expectEquals(-1, $noinline$SignedIntRem01(-13));
    expectEquals(-1, $noinline$SignedIntRem02(-13));
    expectEquals(1, $noinline$SignedIntRem03(-13));
    expectEquals(1, $noinline$SignedIntRem04(-13, true));
    expectEquals(0, $noinline$SignedIntRem05(-12, 0,-13));
    expectEquals(-1, $noinline$SignedIntRem06(-13));
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

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$IntALenRemBy18(int[]) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            lsr{{s?}} r{{\d+}}, #2
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #18
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntALenRemBy18(int[]) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            mov w{{\d+}}, #0x12
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntALenRemBy18(int[] arr) {
    int r = arr.length % 18;
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

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$IntALenRemBy7(int[]) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            add{{s?}} r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            lsr{{s?}} r{{\d+}}, #2
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #7
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntALenRemBy7(int[]) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            mov w{{\d+}}, #0x7
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntALenRemBy7(int[] arr) {
    int r = arr.length % 7;
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

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$IntALenRemBy6(int[]) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$IntALenRemBy6(int[]) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            mov w{{\d+}}, #0x6
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$IntALenRemBy6(int[] arr) {
    int r = arr.length % 6;
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

  private static int $noinline$Negate(int v) {
    return -v;
  }

  private static int $noinline$Decrement(int v) {
    return v - 1;
  }

  private static int $noinline$Increment(int v) {
    return v + 1;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$UnsignedIntRem01(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$UnsignedIntRem01(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            mov w{{\d+}}, #0x6
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$UnsignedIntRem01(int v) {
    int c = 0;
    if (v > 0) {
      c = v % 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$UnsignedIntRem02(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$UnsignedIntRem02(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            mov w{{\d+}}, #0x6
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$UnsignedIntRem02(int v) {
    int c = 0;
    if (0 < v) {
      c = v % 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$UnsignedIntRem03(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$UnsignedIntRem03(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            mov w{{\d+}}, #0x6
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$UnsignedIntRem03(int v) {
    int c = 0;
    if (v >= 0) {
      c = v % 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$UnsignedIntRem04(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$UnsignedIntRem04(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            mov w{{\d+}}, #0x6
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$UnsignedIntRem04(int v) {
    int c = 0;
    if (0 <= v) {
      c = v % 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$UnsignedIntRem05(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            lsr{{s?}} r{{\d+}}, #2
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #10
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$UnsignedIntRem05(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NEXT:            mov w{{\d+}}, #0xa
  /// CHECK-NEXT:            msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$UnsignedIntRem05(int v) {
    int c = 0;
    for(; v > 100; ++c) {
      v %= 10;
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$UnsignedIntRem06(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            lsr{{s?}} r{{\d+}}, r{{\d+}}, #2
  /// CHECK:                 mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$UnsignedIntRem06(int) disassembly (after)
  /// CHECK:                 smull x{{\d+}}, w{{\d+}}, w{{\d+}}
  /// CHECK-NEXT:            lsr x{{\d+}}, x{{\d+}}, #34
  /// CHECK:                 msub w{{\d+}}, w{{\d+}}, w{{\d+}}, w{{\d+}}
  private static int $noinline$UnsignedIntRem06(int v) {
    if (v < 10) {
      v = $noinline$Negate(v); // This is to prevent from using Select.
    } else {
      v = (v % 10) + (v / 10);
    }
    return v;
  }

  // A test case to check that a correcting 'add' is generated for a negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$SignedIntRem01(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, asr #31
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$SignedIntRem01(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntRem01(int v) {
    int c = 0;
    if (v < 0) {
      c = v % 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for a negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$SignedIntRem02(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, asr #31
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$SignedIntRem02(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntRem02(int v) {
    int c = 0;
    if (v <= 0) {
      c = v % 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$SignedIntRem03(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, asr #31
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$SignedIntRem03(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntRem03(int v) {
    boolean positive = (v > 0);
    int c = v % 6;
    if (!positive) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$SignedIntRem04(int, boolean) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, asr #31
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$SignedIntRem04(int, boolean) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntRem04(int v, boolean apply_rem) {
    int c = 0;
    boolean positive = (v > 0);
    if (apply_rem) {
      c = v % 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    if (!positive) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$SignedIntRem05(int, int, int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, asr #31
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$SignedIntRem05(int, int, int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntRem05(int v, int a, int b) {
    int c = 0;

    if (v < a)
      c = $noinline$Increment(c); // This is to prevent from using Select.

    if (b < a)
      c = $noinline$Increment(c); // This is to prevent from using Select.

    if (v > b) {
      c = v % 6;
    } else {
      c = $noinline$Increment(c); // This is to prevent from using Select.
    }

    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM:   int RemTest.$noinline$SignedIntRem06(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, asr #31
  /// CHECK-NEXT:            mov{{s?}} r{{\d+}}, #6
  /// CHECK-NEXT:            mls       r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: int RemTest.$noinline$SignedIntRem06(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntRem06(int v) {
    int c = v % 6;

    if (v > 0) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }

    return c;
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

    expectEquals(1L, $noinline$UnsignedLongRem01(13L));
    expectEquals(1L, $noinline$UnsignedLongRem02(13L));
    expectEquals(1L, $noinline$UnsignedLongRem03(13L));
    expectEquals(1L, $noinline$UnsignedLongRem04(13L));
    expectEquals(1L, $noinline$UnsignedLongRem05(101L));
    expectEquals(11L, $noinline$UnsignedLongRem06(101L));

    expectEquals(-1L, $noinline$SignedLongRem01(-13L));
    expectEquals(-1L, $noinline$SignedLongRem02(-13L));
    expectEquals(1L, $noinline$SignedLongRem03(-13L));
    expectEquals(1L, $noinline$SignedLongRem04(-13L, true));
    expectEquals(0L, $noinline$SignedLongRem05(-12L, 0L,-13L));
    expectEquals(-1L, $noinline$SignedLongRem06(-13L));
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

  private static long $noinline$Negate(long v) {
    return -v;
  }

  private static long $noinline$Decrement(long v) {
    return v - 1;
  }

  private static long $noinline$Increment(long v) {
    return v + 1;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$UnsignedLongRem01(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$UnsignedLongRem01(long v) {
    long c = 0;
    if (v > 0) {
      c = v % 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$UnsignedLongRem02(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$UnsignedLongRem02(long v) {
    long c = 0;
    if (0 < v) {
      c = v % 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$UnsignedLongRem03(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$UnsignedLongRem03(long v) {
    long c = 0;
    if (v >= 0) {
      c = v % 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$UnsignedLongRem04(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$UnsignedLongRem04(long v) {
    long c = 0;
    if (0 <= v) {
      c = v % 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$UnsignedLongRem05(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            lsr x{{\d+}}, x{{\d+}}, #2
  /// CHECK-NEXT:            mov x{{\d+}}, #0xa
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$UnsignedLongRem05(long v) {
    long c = 0;
    for(; v > 100; ++c) {
      v %= 10;
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$UnsignedLongRem06(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            lsr x{{\d+}}, x{{\d+}}, #2
  /// CHECK:                 msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$UnsignedLongRem06(long v) {
    if (v < 10) {
      v = $noinline$Negate(v); // This is to prevent from using Select.
    } else {
      v = (v % 10) + (v / 10);
    }
    return v;
  }

  // A test case to check that a correcting 'add' is generated for a negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$SignedLongRem01(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$SignedLongRem01(long v) {
    long c = 0;
    if (v < 0) {
      c = v % 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for a negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$SignedLongRem02(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$SignedLongRem02(long v) {
    long c = 0;
    if (v <= 0) {
      c = v % 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$SignedLongRem03(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$SignedLongRem03(long v) {
    boolean positive = (v > 0);
    long c = v % 6;
    if (!positive) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$SignedLongRem04(long, boolean) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$SignedLongRem04(long v, boolean apply_rem) {
    long c = 0;
    boolean positive = (v > 0);
    if (apply_rem) {
      c = v % 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    if (!positive) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$SignedLongRem05(long, long, long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$SignedLongRem05(long v, long a, long b) {
    long c = 0;

    if (v < a)
      c = $noinline$Increment(c); // This is to prevent from using Select.

    if (b < a)
      c = $noinline$Increment(c); // This is to prevent from using Select.

    if (v > b) {
      c = v % 6;
    } else {
      c = $noinline$Increment(c); // This is to prevent from using Select.
    }

    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM64: long RemTest.$noinline$SignedLongRem06(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            mov x{{\d+}}, #0x6
  /// CHECK-NEXT:            msub x{{\d+}}, x{{\d+}}, x{{\d+}}, x{{\d+}}
  private static long $noinline$SignedLongRem06(long v) {
    long c = v % 6;

    if (v > 0) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }

    return c;
  }
}
