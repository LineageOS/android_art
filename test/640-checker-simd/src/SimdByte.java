/*
 * Copyright (C) 2017 The Android Open Source Project
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
 * Functional tests for SIMD vectorization.
 */
public class SimdByte {

  static byte[] a;

  //
  // Arithmetic operations.
  //

  /// CHECK-START: void SimdByte.add(int) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-{ARM,ARM64}: void SimdByte.add(int) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecAdd   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void add(int x) {
    for (int i = 0; i < 128; i++)
      a[i] += x;
  }

  /// CHECK-START: void SimdByte.sub(int) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-{ARM,ARM64}: void SimdByte.sub(int) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecSub   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void sub(int x) {
    for (int i = 0; i < 128; i++)
      a[i] -= x;
  }

  /// CHECK-START: void SimdByte.mul(int) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-{ARM,ARM64}: void SimdByte.mul(int) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecMul   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void mul(int x) {
    for (int i = 0; i < 128; i++)
      a[i] *= x;
  }

  /// CHECK-START: void SimdByte.div(int) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: void SimdByte.div(int) loop_optimization (after)
  //
  //  Not supported on any architecture.
  //
  static void div(int x) {
    for (int i = 0; i < 128; i++)
      a[i] /= x;
  }

  /// CHECK-START: void SimdByte.neg() loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-{ARM,ARM64}: void SimdByte.neg() loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecNeg   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void neg() {
    for (int i = 0; i < 128; i++)
      a[i] = (byte) -a[i];
  }

  /// CHECK-START: void SimdByte.not() loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-{ARM,ARM64}: void SimdByte.not() loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecNot   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void not() {
    for (int i = 0; i < 128; i++)
      a[i] = (byte) ~a[i];
  }

  /// CHECK-START: void SimdByte.shl4() loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-{ARM,ARM64}: void SimdByte.shl4() loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecShl   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void shl4() {
    for (int i = 0; i < 128; i++)
      a[i] <<= 4;
  }

  /// CHECK-START: void SimdByte.sar2() loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-{ARM,ARM64}: void SimdByte.sar2() loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecShr   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void sar2() {
    for (int i = 0; i < 128; i++)
      a[i] >>= 2;
  }

  /// CHECK-START: void SimdByte.shr2() loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  // TODO: would need signess flip.
  /// CHECK-START: void SimdByte.shr2() loop_optimization (after)
  /// CHECK-NOT: VecUShr
  static void shr2() {
    for (int i = 0; i < 128; i++)
      a[i] >>>= 2;
  }

  //
  // Shift sanity.
  //

  static void sar31() {
    for (int i = 0; i < 128; i++)
      a[i] >>= 31;
  }

  static void shr31() {
    for (int i = 0; i < 128; i++)
      a[i] >>>= 31;
  }

  static void shr32() {
    for (int i = 0; i < 128; i++)
      a[i] >>>= 32;  // 0, since & 31
  }

  static void shr33() {
    for (int i = 0; i < 128; i++)
      a[i] >>>= 33;  // 1, since & 31
  }

  static void shl9() {
    for (int i = 0; i < 128; i++)
      a[i] <<= 9;  // yields all-zeros
  }

  //
  // Loop bounds.
  //

  static void bounds() {
    for (int i = 1; i < 127; i++)
      a[i] += 11;
  }

  //
  // Test Driver.
  //

  public static void main() {
    // Set up.
    a = new byte[128];
    for (int i = 0; i < 128; i++) {
      a[i] = (byte) i;
    }
    // Arithmetic operations.
    add(2);
    for (int i = 0; i < 128; i++) {
      expectEquals((byte)(i + 2), a[i], "add");
    }
    sub(2);
    for (int i = 0; i < 128; i++) {
      expectEquals(i, a[i], "sub");
    }
    mul(2);
    for (int i = 0; i < 128; i++) {
      expectEquals((byte)(i + i), a[i], "mul");
    }
    div(2);
    for (int i = 0; i < 128; i++) {
      expectEquals(((byte)(i + i)) >> 1, a[i], "div");
      a[i] = (byte) i;  // undo arithmetic wrap-around effects
    }
    neg();
    for (int i = 0; i < 128; i++) {
      expectEquals(-i, a[i], "neg");
    }
    // Loop bounds.
    bounds();
    expectEquals(0, a[0], "bounds0");
    for (int i = 1; i < 127; i++) {
      expectEquals(11 - i, a[i], "bounds");
    }
    expectEquals(-127, a[127], "bounds127");
    // Shifts.
    for (int i = 0; i < 128; i++) {
      a[i] = (byte) 0xff;
    }
    shl4();
    for (int i = 0; i < 128; i++) {
      expectEquals((byte) 0xf0, a[i], "shl4");
    }
    sar2();
    for (int i = 0; i < 128; i++) {
      expectEquals((byte) 0xfc, a[i], "sar2");
    }
    shr2();
    for (int i = 0; i < 128; i++) {
      expectEquals((byte) 0xff, a[i], "shr2");  // sic!
    }
    sar31();
    for (int i = 0; i < 128; i++) {
      expectEquals((byte) 0xff, a[i], "sar31");
    }
    shr31();
    for (int i = 0; i < 128; i++) {
      expectEquals(0x01, a[i], "shr31");
      a[i] = (byte) 0x12;  // reset
    }
    shr32();
    for (int i = 0; i < 128; i++) {
      expectEquals((byte) 0x12, a[i], "shr32");
    }
    shr33();
    for (int i = 0; i < 128; i++) {
      expectEquals((byte) 0x09, a[i], "shr33");
    }
    shl9();
    for (int i = 0; i < 128; i++) {
      expectEquals((byte) 0x00, a[i], "shl9");
      a[i] = (byte) 0xf0;  // reset
    }
    not();
    for (int i = 0; i < 128; i++) {
      expectEquals((byte) 0x0f, a[i], "not");
    }
    // Done.
    System.out.println("SimdByte passed");
  }

  private static void expectEquals(int expected, int result, String action) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result + " for " + action);
    }
  }
}
