/*
 * Copyright (C) 2019 The Android Open Source Project
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
 * Test code generation for BoundsCheck.
 */
public class Main {
  // Constant index, variable length.
  /// CHECK-START-ARM64: int Main.constantIndex(int[]) disassembly (after)
  /// CHECK:                     BoundsCheck
  /// CHECK:                     cmp {{w\d+}}, #0x0
  /// CHECK:                     b.ls #+0x{{[0-9a-f]+}} (addr 0x<<SLOW:[0-9a-f]+>>)
  /// CHECK:                     BoundsCheckSlowPathARM64
  /// CHECK-NEXT:                0x{{0*}}<<SLOW>>:
  /// CHECK-START-ARM: int Main.constantIndex(int[]) disassembly (after)
  /// CHECK:                     BoundsCheck
  /// CHECK:                     cmp {{r\d+}}, #0
  /// CHECK:                     bls.w <<SLOW:0x[0-9a-f]+>>
  /// CHECK:                     BoundsCheckSlowPathARMVIXL
  /// CHECK-NEXT:                <<SLOW>>:
  public static int constantIndex(int[] a) {
    try {
      a[0] = 42;
    } catch (ArrayIndexOutOfBoundsException expected) {
      return -1;
    }
    return a.length;
  }

  // Constant length, variable index.
  /// CHECK-START-ARM64: int Main.constantLength(int) disassembly (after)
  /// CHECK:                     BoundsCheck
  /// CHECK:                     cmp {{w\d+}}, #0xa
  /// CHECK:                     b.hs #+0x{{[0-9a-f]+}} (addr 0x<<SLOW:[0-9a-f]+>>)
  /// CHECK:                     BoundsCheckSlowPathARM64
  /// CHECK-NEXT:                0x{{0*}}<<SLOW>>:
  /// CHECK-START-ARM: int Main.constantLength(int) disassembly (after)
  /// CHECK:                     BoundsCheck
  /// CHECK:                     cmp {{r\d+}}, #10
  /// CHECK:                     bcs.w <<SLOW:0x[0-9a-f]+>>
  /// CHECK:                     BoundsCheckSlowPathARMVIXL
  /// CHECK-NEXT:                <<SLOW>>:
  public static int constantLength(int index) {
    int[] a = new int[10];
    try {
      a[index] = 1;
    } catch (ArrayIndexOutOfBoundsException expected) {
      return -1;
    }
    return index;
  }

  // Constant index and length, out of bounds access. Check that we only have
  // the slow path.
  /// CHECK-START-ARM64: int Main.constantIndexAndLength() disassembly (after)
  /// CHECK:                     BoundsCheck
  /// CHECK-NOT:                 cmp
  /// CHECK:                     b #+0x{{[0-9a-f]+}} (addr 0x<<SLOW:[0-9a-f]+>>)
  /// CHECK:                     BoundsCheckSlowPathARM64
  /// CHECK-NEXT:                0x{{0*}}<<SLOW>>:
  /// CHECK-START-ARM: int Main.constantIndexAndLength() disassembly (after)
  /// CHECK:                     BoundsCheck
  /// CHECK-NOT:                 cmp
  /// CHECK:                     b <<SLOW:0x[0-9a-f]+>>
  /// CHECK:                     BoundsCheckSlowPathARMVIXL
  /// CHECK-NEXT:                <<SLOW>>:
  public static int constantIndexAndLength() {
    try {
      int[] a = new int[5];
      a[10] = 42;
    } catch (ArrayIndexOutOfBoundsException expected) {
      return -1;
    }
    return 0;
  }

  public static void main(String[] args) {
    int[] a = new int[10];
    int[] b = new int[0];
    expectEquals(a.length, constantIndex(a));
    expectEquals(-1, constantIndex(b));
    expectEquals(0, constantLength(0));
    expectEquals(9, constantLength(9));
    expectEquals(-1, constantLength(10));
    expectEquals(-1, constantLength(-2));
    expectEquals(-1, constantIndexAndLength());
    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
