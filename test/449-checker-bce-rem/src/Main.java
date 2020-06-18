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

  private static void remInt() {
    expectEquals(1L << 32, $noinline$IntRemBy3(3));
    expectEquals((3L << 32) | 6, $noinline$IntRemBy7(27));
    expectEquals((1L << 32) | 1, $noinline$IntRemBy12(13));
    expectEquals((1L << 32) | 1, $noinline$IntRemBy12A(13));
  }

  // A test case to check:
  //  BCE detects the optimized 'v % 3' and eliminates bounds checks.
  //
  /// CHECK-START:       long Main.$noinline$IntRemBy3(int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK:                 BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       long Main.$noinline$IntRemBy3(int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NOT:             BoundsCheck
  /// CHECK:                 ArrayGet
  private static long $noinline$IntRemBy3(int v) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 3;
      int r = v % 3;
      return ((long)q << 32) | values[r];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  BCE detects the optimized 'v % 7' and eliminates bounds checks.
  //
  /// CHECK-START:       long Main.$noinline$IntRemBy7(int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK:                 BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       long Main.$noinline$IntRemBy7(int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NOT:             BoundsCheck
  /// CHECK:                 ArrayGet
  private static long $noinline$IntRemBy7(int v) {
    int[] values = {0, 1, 2, 3, 4, 5, 6};
    if (v > 0) {
      int q = v / 7;
      int r = v % 7;
      return ((long)q << 32) | values[r];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  BCE detects the optimized 'v % 12' and eliminates bounds checks.
  //
  /// CHECK-START:       long Main.$noinline$IntRemBy12(int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK:                 BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       long Main.$noinline$IntRemBy12(int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NOT:             BoundsCheck
  /// CHECK:                 ArrayGet
  private static long $noinline$IntRemBy12(int v) {
    int[] values = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    if (v > 0) {
      int q = v / 12;
      int r = v % 12;
      return ((long)q << 32) | values[r];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  BCE detects the optimized 'v % 12' and eliminates bounds checks.
  //
  /// CHECK-START:       long Main.$noinline$IntRemBy12A(int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK:                 BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       long Main.$noinline$IntRemBy12A(int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NOT:             BoundsCheck
  /// CHECK:                 ArrayGet
  private static long $noinline$IntRemBy12A(int v) {
    int[] values = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    if (v > 0) {
      int q = v / 12;
      int t = q * 12;
      int r = v  - t;
      return ((long)q << 32) | values[r];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  BCE detects the optimized 'v % Integer.MAX_VALUE' and eliminates bounds checks.
  //
  /// CHECK-START:       int Main.$noinline$IntRemByMaxInt(int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK:                 BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$IntRemByMaxInt(int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NOT:             BoundsCheck
  /// CHECK:                 ArrayGet
  private static int $noinline$IntRemByMaxInt(int v) {
    int[] values = new int[Integer.MAX_VALUE];
    if (v > 0) {
      int q = v / Integer.MAX_VALUE;
      int r = v % Integer.MAX_VALUE;
      return values[v % Integer.MAX_VALUE] + q;
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  BCE detects the optimized 'v % Integer.MIN_VALUE' and eliminates bounds checks.
  //
  /// CHECK-START:       int Main.$noinline$IntRemByMinInt(int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK:                 BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$IntRemByMinInt(int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NOT:             BoundsCheck
  /// CHECK:                 ArrayGet
  private static int $noinline$IntRemByMinInt(int v) {
    int[] values = new int[Integer.MAX_VALUE];
    if (v > 0) {
      int q = v / Integer.MIN_VALUE;
      int t = q * Integer.MIN_VALUE;
      int r = v - t;
      return values[r - 1];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem01(int, int) BCE (before)
  /// CHECK:                 Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem01(int, int) BCE (after)
  /// CHECK:                 Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem01(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int a = v * 10;
      int b = s - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem02(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem02(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem02(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int a = q * s;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem03(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem03(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem03(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int a = q + s;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem04(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem04(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem04(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = q << s;
      int a = q + t;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem05(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem05(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem05(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = s << 1;
      int a = q + t;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem06(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem06(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem06(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int a = q * 11;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem07(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem07(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem07(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = q << 1;
      int a = s + t;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem08(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem08(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem08(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = q << 31;
      int a = q + t;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem09(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem09(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Add
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem09(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = q << 1;
      int a = q + t;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem10(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem10(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem10(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = q << s;
      int a = t - q;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem11(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem11(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem11(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = s << 1;
      int a = t - q;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem12(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem12(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem12(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = q << 1;
      int a = t - s;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem13(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem13(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Shl
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem13(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int t = q << 31;
      int a = t - q;
      int b = v - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem14(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem14(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem14(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int a = v / 10;
      int b = s - a;
      return values[b];
    } else {
      return -1;
    }
  }

  // A test case to check:
  //  Bounds checks are not eliminated if the checked value is not an optimized HDiv+HRem.
  //
  /// CHECK-START:       int Main.$noinline$NoRem15(int, int) BCE (before)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  //
  /// CHECK-START:       int Main.$noinline$NoRem15(int, int) BCE (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            Mul
  /// CHECK-NEXT:            Sub
  /// CHECK-NEXT:            BoundsCheck
  /// CHECK-NEXT:            ArrayGet
  private static int $noinline$NoRem15(int v, int s) {
    int[] values = {0, 1, 2};
    if (v > 0) {
      int q = v / 10;
      int a = q * 10;
      int b = s - a;
      return values[b];
    } else {
      return -1;
    }
  }

  public static void main(String args[]) {
    remInt();
  }
}
