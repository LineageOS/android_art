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

  private static void expectEquals(String expected, String result) {
    if (!expected.equals(result)) {
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

    expectEquals(0, $noinline$IntALenDivBy18(new int[0]));
    expectEquals(0, $noinline$IntALenDivBy18(new int[1]));
    expectEquals(1, $noinline$IntALenDivBy18(new int[18]));
    expectEquals(3, $noinline$IntALenDivBy18(new int[65]));

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

    expectEquals(0, $noinline$IntALenDivBy7(new int[0]));
    expectEquals(0, $noinline$IntALenDivBy7(new int[1]));
    expectEquals(1, $noinline$IntALenDivBy7(new int[7]));
    expectEquals(3, $noinline$IntALenDivBy7(new int[22]));

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

    expectEquals(0, $noinline$IntALenDivBy6(new int[0]));
    expectEquals(0, $noinline$IntALenDivBy6(new int[1]));
    expectEquals(1, $noinline$IntALenDivBy6(new int[6]));
    expectEquals(3, $noinline$IntALenDivBy6(new int[19]));

    expectEquals(0, $noinline$IntDivByMinus6(0));
    expectEquals(0, $noinline$IntDivByMinus6(1));
    expectEquals(0, $noinline$IntDivByMinus6(-1));
    expectEquals(-1, $noinline$IntDivByMinus6(6));
    expectEquals(1, $noinline$IntDivByMinus6(-6));
    expectEquals(-3, $noinline$IntDivByMinus6(19));
    expectEquals(3, $noinline$IntDivByMinus6(-19));

    expectEquals(2, $noinline$UnsignedIntDiv01(12));
    expectEquals(2, $noinline$UnsignedIntDiv02(12));
    expectEquals(2, $noinline$UnsignedIntDiv03(12));
    expectEquals(2, $noinline$UnsignedIntDiv04(12));
    expectEquals("01", $noinline$UnsignedIntDiv05(10));
    expectEquals("321", $noinline$UnsignedIntDiv05(123));
    expectEquals(1, $noinline$UnsignedIntDiv06(101));
    expectEquals(1, $noinline$UnsignedIntDiv07(10));
    expectEquals(1, $noinline$UnsignedIntDiv07(100));
    expectEquals(10, $noinline$UnsignedIntDiv08(100));
    expectEquals(11, $noinline$UnsignedIntDiv08(101));

    expectEquals(-2, $noinline$SignedIntDiv01(-12));
    expectEquals(-2, $noinline$SignedIntDiv02(-12));
    expectEquals(2, $noinline$SignedIntDiv03(-12));
    expectEquals(2, $noinline$SignedIntDiv04(-12, true));
    expectEquals(-2, $noinline$SignedIntDiv05(-12, 0,-13));
    expectEquals(-2, $noinline$SignedIntDiv06(-12));
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

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$IntALenDivBy18(int[]) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            lsr{{s?}} r{{\d+}}, r{{\d+}}, #2
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntALenDivBy18(int[]) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NOT:             add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$IntALenDivBy18(int[] arr) {
    int r = arr.length / 18;
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

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$IntALenDivBy7(int[]) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            add{{s?}} r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            lsr{{s?}} r{{\d+}}, r{{\d+}}, #2
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntALenDivBy7(int[]) disassembly (after)
  /// CHECK:                 adds x{{\d+}}, x{{\d+}}, x{{\d+}}, lsl #32
  /// CHECK-NEXT:            lsr  x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NOT:             cinc w{{\d+}}, w{{\d+}}, mi
  private static int $noinline$IntALenDivBy7(int[] arr) {
    int r = arr.length / 7;
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

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$IntALenDivBy6(int[]) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$IntALenDivBy6(int[]) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NOT:             add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$IntALenDivBy6(int[] arr) {
    int r = arr.length / 6;
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
  /// CHECK-START-ARM:   int DivTest.$noinline$UnsignedIntDiv01(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$UnsignedIntDiv01(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NOT:             add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$UnsignedIntDiv01(int v) {
    int c = 0;
    if (v > 0) {
      c = v / 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$UnsignedIntDiv02(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$UnsignedIntDiv02(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NOT:             add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$UnsignedIntDiv02(int v) {
    int c = 0;
    if (0 < v) {
      c = v / 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$UnsignedIntDiv03(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$UnsignedIntDiv03(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NOT:             add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$UnsignedIntDiv03(int v) {
    int c = 0;
    if (v >= 0) {
      c = v / 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$UnsignedIntDiv04(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$UnsignedIntDiv04(int) disassembly (after)
  /// CHECK:                 lsr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NOT:             add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$UnsignedIntDiv04(int v) {
    int c = 0;
    if (0 <= v) {
      c = v / 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   java.lang.String DivTest.$noinline$UnsignedIntDiv05(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: java.lang.String DivTest.$noinline$UnsignedIntDiv05(int) disassembly (after)
  /// CHECK:                 smull x{{\d+}}, w{{\d+}}, w{{\d+}}
  /// CHECK:                 lsr   x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NOT:             add   w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static String $noinline$UnsignedIntDiv05(int v) {
    String r = "";
    while (v > 0) {
      int d = v % 10;
      r += (char)(d + '0');
      v /= 10;
    }
    return r;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$UnsignedIntDiv06(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$UnsignedIntDiv06(int) disassembly (after)
  /// CHECK:                 smull x{{\d+}}, w{{\d+}}, w{{\d+}}
  /// CHECK:                 lsr   x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NOT:             add   w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$UnsignedIntDiv06(int v) {
    int c = 0;
    for(; v > 100; ++c) {
      v /= 10;
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$UnsignedIntDiv07(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$UnsignedIntDiv07(int) disassembly (after)
  /// CHECK:                 smull x{{\d+}}, w{{\d+}}, w{{\d+}}
  /// CHECK:                 lsr   x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NOT:             add   w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$UnsignedIntDiv07(int v) {
    while (v > 0 && (v % 10) == 0) {
      v /= 10;
    }
    return v;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$UnsignedIntDiv08(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NOT:             sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$UnsignedIntDiv08(int) disassembly (after)
  /// CHECK:                 smull x{{\d+}}, w{{\d+}}, w{{\d+}}
  /// CHECK:                 lsr   x{{\d+}}, x{{\d+}}, #34
  /// CHECK-NOT:             add   w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$UnsignedIntDiv08(int v) {
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
  /// CHECK-START-ARM:   int DivTest.$noinline$SignedIntDiv01(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$SignedIntDiv01(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntDiv01(int v) {
    int c = 0;
    if (v < 0) {
      c = v / 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for a negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$SignedIntDiv02(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$SignedIntDiv02(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntDiv02(int v) {
    int c = 0;
    if (v <= 0) {
      c = v / 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$SignedIntDiv03(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$SignedIntDiv03(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntDiv03(int v) {
    boolean positive = (v > 0);
    int c = v / 6;
    if (!positive) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$SignedIntDiv04(int, boolean) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$SignedIntDiv04(int, boolean) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntDiv04(int v, boolean apply_div) {
    int c = 0;
    boolean positive = (v > 0);
    if (apply_div) {
      c = v / 6;
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
  /// CHECK-START-ARM:   int DivTest.$noinline$SignedIntDiv05(int, int, int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$SignedIntDiv05(int, int, int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntDiv05(int v, int a, int b) {
    int c = 0;

    if (v < a)
      c = $noinline$Increment(c); // This is to prevent from using Select.

    if (b < a)
      c = $noinline$Increment(c); // This is to prevent from using Select.

    if (v > b) {
      c = v / 6;
    } else {
      c = $noinline$Increment(c); // This is to prevent from using Select.
    }

    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM:   int DivTest.$noinline$SignedIntDiv06(int) disassembly (after)
  /// CHECK:                 smull     r{{\d+}}, r{{\d+}}, r{{\d+}}, r{{\d+}}
  /// CHECK-NEXT:            sub       r{{\d+}}, r{{\d+}}, r{{\d+}}, asr #31
  //
  /// CHECK-START-ARM64: int DivTest.$noinline$SignedIntDiv06(int) disassembly (after)
  /// CHECK:                 asr x{{\d+}}, x{{\d+}}, #32
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  private static int $noinline$SignedIntDiv06(int v) {
    int c = v / 6;

    if (v > 0) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }

    return c;
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

    expectEquals(2L, $noinline$UnsignedLongDiv01(12L));
    expectEquals(2L, $noinline$UnsignedLongDiv02(12L));
    expectEquals(2L, $noinline$UnsignedLongDiv03(12L));
    expectEquals(2L, $noinline$UnsignedLongDiv04(12L));
    expectEquals("01", $noinline$UnsignedLongDiv05(10L));
    expectEquals("321", $noinline$UnsignedLongDiv05(123L));
    expectEquals(1L, $noinline$UnsignedLongDiv06(101L));
    expectEquals(1L, $noinline$UnsignedLongDiv07(10L));
    expectEquals(1L, $noinline$UnsignedLongDiv07(100L));
    expectEquals(10L, $noinline$UnsignedLongDiv08(100L));
    expectEquals(11L, $noinline$UnsignedLongDiv08(101L));

    expectEquals(-2L, $noinline$SignedLongDiv01(-12L));
    expectEquals(-2L, $noinline$SignedLongDiv02(-12L));
    expectEquals(2L, $noinline$SignedLongDiv03(-12L));
    expectEquals(2L, $noinline$SignedLongDiv04(-12L, true));
    expectEquals(-2L, $noinline$SignedLongDiv05(-12L, 0L,-13L));
    expectEquals(-2L, $noinline$SignedLongDiv06(-12L));
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
  /// CHECK-START-ARM64: long DivTest.$noinline$UnsignedLongDiv01(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$UnsignedLongDiv01(long v) {
    long c = 0;
    if (v > 0) {
      c = v / 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$UnsignedLongDiv02(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$UnsignedLongDiv02(long v) {
    long c = 0;
    if (0 < v) {
      c = v / 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$UnsignedLongDiv03(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$UnsignedLongDiv03(long v) {
    long c = 0;
    if (v >= 0) {
      c = v / 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$UnsignedLongDiv04(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$UnsignedLongDiv04(long v) {
    long c = 0;
    if (0 <= v) {
      c = v / 6;
    } else {
      c = $noinline$Negate(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: java.lang.String DivTest.$noinline$UnsignedLongDiv05(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            lsr   x{{\d+}}, x{{\d+}}, #2
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static String $noinline$UnsignedLongDiv05(long v) {
    String r = "";
    while (v > 0) {
      long d = v % 10;
      r += (char)(d + '0');
      v /= 10;
    }
    return r;
  }
  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: void DivTest.$noinline$UnsignedLongDiv05(java.lang.StringBuilder, long, long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            lsr   x{{\d+}}, x{{\d+}}, #2
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static void $noinline$UnsignedLongDiv05(java.lang.StringBuilder sb, long w, long d) {
    while (w > 0) {
      sb.append((char)(d/w + '0'));
      d = d % w;
      w /= 10;
    }
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$UnsignedLongDiv06(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$UnsignedLongDiv06(long v) {
    long c = 0;
    for(; v > 100; ++c) {
      v /= 10;
    }
    return c;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$UnsignedLongDiv07(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$UnsignedLongDiv07(long v) {
    while (v > 0 && (v % 10) == 0) {
      v /= 10;
    }
    return v;
  }

  // A test case to check that a correcting 'add' is not generated for a non-negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$UnsignedLongDiv08(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NOT:             add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$UnsignedLongDiv08(long v) {
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
  /// CHECK-START-ARM64: long DivTest.$noinline$SignedLongDiv01(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$SignedLongDiv01(long v) {
    long c = 0;
    if (v < 0) {
      c = v / 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for a negative
  // dividend and a positive divisor.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$SignedLongDiv02(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$SignedLongDiv02(long v) {
    long c = 0;
    if (v <= 0) {
      c = v / 6;
    } else {
      c = $noinline$Decrement(v); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$SignedLongDiv03(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$SignedLongDiv03(long v) {
    boolean positive = (v > 0);
    long c = v / 6;
    if (!positive) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }
    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$SignedLongDiv04(long, boolean) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$SignedLongDiv04(long v, boolean apply_div) {
    long c = 0;
    boolean positive = (v > 0);
    if (apply_div) {
      c = v / 6;
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
  /// CHECK-START-ARM64: long DivTest.$noinline$SignedLongDiv05(long, long, long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$SignedLongDiv05(long v, long a, long b) {
    long c = 0;

    if (v < a)
      c = $noinline$Increment(c); // This is to prevent from using Select.

    if (b < a)
      c = $noinline$Increment(c); // This is to prevent from using Select.

    if (v > b) {
      c = v / 6;
    } else {
      c = $noinline$Increment(c); // This is to prevent from using Select.
    }

    return c;
  }

  // A test case to check that a correcting 'add' is generated for signed division.
  //
  /// CHECK-START-ARM64: long DivTest.$noinline$SignedLongDiv06(long) disassembly (after)
  /// CHECK:                 smulh x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  private static long $noinline$SignedLongDiv06(long v) {
    long c = v / 6;

    if (v > 0) {
      c = $noinline$Negate(c); // This is to prevent from using Select.
    }

    return c;
  }
}
