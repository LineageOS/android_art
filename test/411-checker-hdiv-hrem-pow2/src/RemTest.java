/*
 * Copyright (C) 2018 The Android Open Source Project
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

  public static <T extends Number> void expectEquals(T expected, T result) {
    if (!expected.equals(result)) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void main() {
    remInt();
    remLong();
  }

  private static void remInt() {
    expectEquals(0, $noinline$IntMod2(0));
    expectEquals(1, $noinline$IntMod2(1));
    expectEquals(-1, $noinline$IntMod2(-1));
    expectEquals(0, $noinline$IntMod2(2));
    expectEquals(0, $noinline$IntMod2(-2));
    expectEquals(1, $noinline$IntMod2(3));
    expectEquals(-1, $noinline$IntMod2(-3));
    expectEquals(1, $noinline$IntMod2(0x0f));
    expectEquals(1, $noinline$IntMod2(0x00ff));
    expectEquals(1, $noinline$IntMod2(0x00ffff));
    expectEquals(1, $noinline$IntMod2(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntMod2(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntModMinus2(0));
    expectEquals(1, $noinline$IntModMinus2(1));
    expectEquals(-1, $noinline$IntModMinus2(-1));
    expectEquals(0, $noinline$IntModMinus2(2));
    expectEquals(0, $noinline$IntModMinus2(-2));
    expectEquals(1, $noinline$IntModMinus2(3));
    expectEquals(-1, $noinline$IntModMinus2(-3));
    expectEquals(1, $noinline$IntModMinus2(0x0f));
    expectEquals(1, $noinline$IntModMinus2(0x00ff));
    expectEquals(1, $noinline$IntModMinus2(0x00ffff));
    expectEquals(1, $noinline$IntModMinus2(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntModMinus2(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsMod2(0));
    expectEquals(1, $noinline$IntAbsMod2(1));
    expectEquals(1, $noinline$IntAbsMod2(-1));
    expectEquals(0, $noinline$IntAbsMod2(2));
    expectEquals(0, $noinline$IntAbsMod2(-2));
    expectEquals(1, $noinline$IntAbsMod2(3));
    expectEquals(1, $noinline$IntAbsMod2(-3));
    expectEquals(1, $noinline$IntAbsMod2(0x0f));
    expectEquals(1, $noinline$IntAbsMod2(0x00ff));
    expectEquals(1, $noinline$IntAbsMod2(0x00ffff));
    expectEquals(1, $noinline$IntAbsMod2(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntAbsMod2(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsModMinus2(0));
    expectEquals(1, $noinline$IntAbsModMinus2(1));
    expectEquals(1, $noinline$IntAbsModMinus2(-1));
    expectEquals(0, $noinline$IntAbsModMinus2(2));
    expectEquals(0, $noinline$IntAbsModMinus2(-2));
    expectEquals(1, $noinline$IntAbsModMinus2(3));
    expectEquals(1, $noinline$IntAbsModMinus2(-3));
    expectEquals(1, $noinline$IntAbsModMinus2(0x0f));
    expectEquals(1, $noinline$IntAbsModMinus2(0x00ff));
    expectEquals(1, $noinline$IntAbsModMinus2(0x00ffff));
    expectEquals(1, $noinline$IntAbsModMinus2(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntAbsModMinus2(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntALenMod2(new int[0]));
    expectEquals(1, $noinline$IntALenMod2(new int[1]));
    expectEquals(0, $noinline$IntALenMod2(new int[2]));
    expectEquals(1, $noinline$IntALenMod2(new int[3]));
    expectEquals(1, $noinline$IntALenMod2(new int[0x0f]));
    expectEquals(1, $noinline$IntALenMod2(new int[0x00ff]));
    expectEquals(1, $noinline$IntALenMod2(new int[0x00ffff]));

    expectEquals(0, $noinline$IntALenModMinus2(new int[0]));
    expectEquals(1, $noinline$IntALenModMinus2(new int[1]));
    expectEquals(0, $noinline$IntALenModMinus2(new int[2]));
    expectEquals(1, $noinline$IntALenModMinus2(new int[3]));
    expectEquals(1, $noinline$IntALenModMinus2(new int[0x0f]));
    expectEquals(1, $noinline$IntALenModMinus2(new int[0x00ff]));
    expectEquals(1, $noinline$IntALenModMinus2(new int[0x00ffff]));

    expectEquals(0, $noinline$IntMod16(0));
    expectEquals(1, $noinline$IntMod16(1));
    expectEquals(1, $noinline$IntMod16(17));
    expectEquals(-1, $noinline$IntMod16(-1));
    expectEquals(0, $noinline$IntMod16(32));
    expectEquals(0, $noinline$IntMod16(-32));
    expectEquals(0x0f, $noinline$IntMod16(0x0f));
    expectEquals(0x0f, $noinline$IntMod16(0x00ff));
    expectEquals(0x0f, $noinline$IntMod16(0x00ffff));
    expectEquals(15, $noinline$IntMod16(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntMod16(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntModMinus16(0));
    expectEquals(1, $noinline$IntModMinus16(1));
    expectEquals(1, $noinline$IntModMinus16(17));
    expectEquals(-1, $noinline$IntModMinus16(-1));
    expectEquals(0, $noinline$IntModMinus16(32));
    expectEquals(0, $noinline$IntModMinus16(-32));
    expectEquals(0x0f, $noinline$IntModMinus16(0x0f));
    expectEquals(0x0f, $noinline$IntModMinus16(0x00ff));
    expectEquals(0x0f, $noinline$IntModMinus16(0x00ffff));
    expectEquals(15, $noinline$IntModMinus16(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntModMinus16(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsMod16(0));
    expectEquals(1, $noinline$IntAbsMod16(1));
    expectEquals(1, $noinline$IntAbsMod16(17));
    expectEquals(1, $noinline$IntAbsMod16(-1));
    expectEquals(0, $noinline$IntAbsMod16(32));
    expectEquals(0, $noinline$IntAbsMod16(-32));
    expectEquals(0x0f, $noinline$IntAbsMod16(0x0f));
    expectEquals(0x0f, $noinline$IntAbsMod16(0x00ff));
    expectEquals(0x0f, $noinline$IntAbsMod16(0x00ffff));
    expectEquals(15, $noinline$IntAbsMod16(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntAbsMod16(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsModMinus16(0));
    expectEquals(1, $noinline$IntAbsModMinus16(1));
    expectEquals(1, $noinline$IntAbsModMinus16(17));
    expectEquals(1, $noinline$IntAbsModMinus16(-1));
    expectEquals(0, $noinline$IntAbsModMinus16(32));
    expectEquals(0, $noinline$IntAbsModMinus16(-32));
    expectEquals(0x0f, $noinline$IntAbsModMinus16(0x0f));
    expectEquals(0x0f, $noinline$IntAbsModMinus16(0x00ff));
    expectEquals(0x0f, $noinline$IntAbsModMinus16(0x00ffff));
    expectEquals(15, $noinline$IntAbsModMinus16(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntAbsModMinus16(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntALenMod16(new int[0]));
    expectEquals(1, $noinline$IntALenMod16(new int[1]));
    expectEquals(1, $noinline$IntALenMod16(new int[17]));
    expectEquals(0, $noinline$IntALenMod16(new int[32]));
    expectEquals(0x0f, $noinline$IntALenMod16(new int[0x0f]));
    expectEquals(0x0f, $noinline$IntALenMod16(new int[0x00ff]));
    expectEquals(0x0f, $noinline$IntALenMod16(new int[0x00ffff]));

    expectEquals(0, $noinline$IntALenModMinus16(new int[0]));
    expectEquals(1, $noinline$IntALenModMinus16(new int[1]));
    expectEquals(1, $noinline$IntALenModMinus16(new int[17]));
    expectEquals(0, $noinline$IntALenModMinus16(new int[32]));
    expectEquals(0x0f, $noinline$IntALenModMinus16(new int[0x0f]));
    expectEquals(0x0f, $noinline$IntALenModMinus16(new int[0x00ff]));
    expectEquals(0x0f, $noinline$IntALenModMinus16(new int[0x00ffff]));

    expectEquals(0, $noinline$IntAbsMod1024(0));
    expectEquals(1, $noinline$IntAbsMod1024(1));
    expectEquals(1, $noinline$IntAbsMod1024(1025));
    expectEquals(1, $noinline$IntAbsMod1024(-1));
    expectEquals(0, $noinline$IntAbsMod1024(2048));
    expectEquals(0, $noinline$IntAbsMod1024(-2048));
    expectEquals(0x0f, $noinline$IntAbsMod1024(0x0f));
    expectEquals(0x0ff, $noinline$IntAbsMod1024(0x00ff));
    expectEquals(0x03ff, $noinline$IntAbsMod1024(0x00ffff));
    expectEquals(0x03ff, $noinline$IntAbsMod1024(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntAbsMod1024(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsModMinus1024(0));
    expectEquals(1, $noinline$IntAbsModMinus1024(1));
    expectEquals(1, $noinline$IntAbsModMinus1024(1025));
    expectEquals(1, $noinline$IntAbsModMinus1024(-1));
    expectEquals(0, $noinline$IntAbsModMinus1024(2048));
    expectEquals(0, $noinline$IntAbsModMinus1024(-2048));
    expectEquals(0x0f, $noinline$IntAbsModMinus1024(0x0f));
    expectEquals(0x0ff, $noinline$IntAbsModMinus1024(0x00ff));
    expectEquals(0x03ff, $noinline$IntAbsModMinus1024(0x00ffff));
    expectEquals(0x03ff, $noinline$IntAbsModMinus1024(Integer.MAX_VALUE));
    expectEquals(0, $noinline$IntAbsModMinus1024(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntALenMod1024(new int[0]));
    expectEquals(1, $noinline$IntALenMod1024(new int[1]));
    expectEquals(1, $noinline$IntALenMod1024(new int[1025]));
    expectEquals(0, $noinline$IntALenMod1024(new int[2048]));
    expectEquals(0x0f, $noinline$IntALenMod1024(new int[0x0f]));
    expectEquals(0x0ff, $noinline$IntALenMod1024(new int[0x00ff]));
    expectEquals(0x03ff, $noinline$IntALenMod1024(new int[0x00ffff]));

    expectEquals(0, $noinline$IntALenModMinus1024(new int[0]));
    expectEquals(1, $noinline$IntALenModMinus1024(new int[1]));
    expectEquals(1, $noinline$IntALenModMinus1024(new int[1025]));
    expectEquals(0, $noinline$IntALenModMinus1024(new int[2048]));
    expectEquals(0x0f, $noinline$IntALenModMinus1024(new int[0x0f]));
    expectEquals(0x0ff, $noinline$IntALenModMinus1024(new int[0x00ff]));
    expectEquals(0x03ff, $noinline$IntALenModMinus1024(new int[0x00ffff]));

    expectEquals(0, $noinline$IntModIntMin(0));
    expectEquals(1, $noinline$IntModIntMin(1));
    expectEquals(0, $noinline$IntModIntMin(Integer.MIN_VALUE));
    expectEquals(-1, $noinline$IntModIntMin(-1));
    expectEquals(0x0f, $noinline$IntModIntMin(0x0f));
    expectEquals(0x00ff, $noinline$IntModIntMin(0x00ff));
    expectEquals(0x00ffff, $noinline$IntModIntMin(0x00ffff));
    expectEquals(Integer.MAX_VALUE, $noinline$IntModIntMin(Integer.MAX_VALUE));

    expectEquals(0, $noinline$IntAbsModIntMin(0));
    expectEquals(1, $noinline$IntAbsModIntMin(1));
    expectEquals(0, $noinline$IntAbsModIntMin(Integer.MIN_VALUE));
    expectEquals(1, $noinline$IntAbsModIntMin(-1));
    expectEquals(0x0f, $noinline$IntAbsModIntMin(0x0f));
    expectEquals(0x00ff, $noinline$IntAbsModIntMin(0x00ff));
    expectEquals(0x00ffff, $noinline$IntAbsModIntMin(0x00ffff));
    expectEquals(Integer.MAX_VALUE, $noinline$IntAbsModIntMin(Integer.MAX_VALUE));
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntMod2(int) disassembly (after)
  /// CHECK:                 add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #31
  /// CHECK-NEXT:            bfc       r{{\d+}}, #0, #1
  /// CHECK-NEXT:            sub{{s?}} r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntMod2(int) disassembly (after)
  /// CHECK:                 cmp w{{\d+}}, #0x0
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x1
  /// CHECK-NEXT:            cneg w{{\d+}}, w{{\d+}}, lt
  //
  /// CHECK-START-X86_64: java.lang.Integer RemTest.$noinline$IntMod2(int) disassembly (after)
  /// CHECK:          Rem [{{i\d+}},{{i\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shr
  /// CHECK-NOT:      imul
  /// CHECK:          mov
  /// CHECK:          and
  /// CHECK:          jz/eq
  /// CHECK:          lea
  /// CHECK:          test
  /// CHECK:          cmovl/nge
  private static Integer $noinline$IntMod2(int v) {
    int r = v % 2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntModMinus2(int) disassembly (after)
  /// CHECK:                 add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #31
  /// CHECK-NEXT:            bfc       r{{\d+}}, #0, #1
  /// CHECK-NEXT:            sub{{s?}} r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntModMinus2(int) disassembly (after)
  /// CHECK:                 cmp w{{\d+}}, #0x0
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x1
  /// CHECK-NEXT:            cneg w{{\d+}}, w{{\d+}}, lt
  //
  /// CHECK-START-X86_64: java.lang.Integer RemTest.$noinline$IntModMinus2(int) disassembly (after)
  /// CHECK:          Rem [{{i\d+}},{{i\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shr
  /// CHECK-NOT:      imul
  /// CHECK:          mov
  /// CHECK:          and
  /// CHECK:          jz/eq
  /// CHECK:          lea
  /// CHECK:          test
  /// CHECK:          cmovl/nge
  private static Integer $noinline$IntModMinus2(int v) {
    int r = v % -2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntAbsMod2(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and{{s?}} r{{\d+}}, r{{\d+}}, #0x1
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntAbsMod2(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x1
  private static Integer $noinline$IntAbsMod2(int v) {
    int r = Math.abs(v) % 2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntAbsModMinus2(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and{{s?}} r{{\d+}}, r{{\d+}}, #0x1
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntAbsModMinus2(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x1
  private static Integer $noinline$IntAbsModMinus2(int v) {
    int r = Math.abs(v) % -2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntALenMod2(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and{{s?}} r{{\d+}}, r{{\d+}}, #0x1
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntALenMod2(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x1
  private static Integer $noinline$IntALenMod2(int[] arr) {
    int r = arr.length % 2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntALenModMinus2(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and{{s?}} r{{\d+}}, r{{\d+}}, #0x1
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntALenModMinus2(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x1
  private static Integer $noinline$IntALenModMinus2(int[] arr) {
    int r = arr.length % -2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntMod16(int) disassembly (after)
  /// CHECK:                 asr{{s?}} r{{\d+}}, r{{\d+}}, #31
  /// CHECK-NEXT:            add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #28
  /// CHECK-NEXT:            bfc       r{{\d+}}, #0, #4
  /// CHECK-NEXT:            sub{{s?}} r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntMod16(int) disassembly (after)
  /// CHECK:                 negs w{{\d+}}, w{{\d+}}
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0xf
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0xf
  /// CHECK-NEXT:            csneg w{{\d+}}, w{{\d+}}, mi
  //
  /// CHECK-START-X86_64: java.lang.Integer RemTest.$noinline$IntMod16(int) disassembly (after)
  /// CHECK:          Rem [{{i\d+}},{{i\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shr
  /// CHECK-NOT:      imul
  /// CHECK:          mov
  /// CHECK:          and
  /// CHECK:          jz/eq
  /// CHECK:          lea
  /// CHECK:          test
  /// CHECK:          cmovl/nge
  private static Integer $noinline$IntMod16(int v) {
    int r = v % 16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntModMinus16(int) disassembly (after)
  /// CHECK:                 asr{{s?}} r{{\d+}}, r{{\d+}}, #31
  /// CHECK-NEXT:            add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #28
  /// CHECK-NEXT:            bfc       r{{\d+}}, #0, #4
  /// CHECK-NEXT:            sub{{s?}} r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntModMinus16(int) disassembly (after)
  /// CHECK:                 negs w{{\d+}}, w{{\d+}}
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0xf
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0xf
  /// CHECK-NEXT:            csneg w{{\d+}}, w{{\d+}}, mi
  //
  /// CHECK-START-X86_64: java.lang.Integer RemTest.$noinline$IntModMinus16(int) disassembly (after)
  /// CHECK:          Rem [{{i\d+}},{{i\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shr
  /// CHECK-NOT:      imul
  /// CHECK:          mov
  /// CHECK:          and
  /// CHECK:          jz/eq
  /// CHECK:          lea
  /// CHECK:          test
  /// CHECK:          cmovl/nge
  private static Integer $noinline$IntModMinus16(int v) {
    int r = v % -16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntAbsMod16(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and{{s?}} r{{\d+}}, r{{\d+}}, #0xf
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntAbsMod16(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0xf
  private static Integer $noinline$IntAbsMod16(int v) {
    int r = Math.abs(v) % 16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntAbsModMinus16(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and{{s?}} r{{\d+}}, r{{\d+}}, #0xf
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntAbsModMinus16(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0xf
  private static Integer $noinline$IntAbsModMinus16(int v) {
    int r = Math.abs(v) % -16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntALenMod16(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and{{s?}} r{{\d+}}, r{{\d+}}, #0xf
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntALenMod16(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0xf
  private static Integer $noinline$IntALenMod16(int[] arr) {
    int r = arr.length % 16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntALenModMinus16(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and{{s?}} r{{\d+}}, r{{\d+}}, #0xf
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntALenModMinus16(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0xf
  private static Integer $noinline$IntALenModMinus16(int[] arr) {
    int r = arr.length % -16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntAbsMod1024(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            ubfx r{{\d+}}, r{{\d+}}, #0, #10
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntAbsMod1024(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x3ff
  private static Integer $noinline$IntAbsMod1024(int v) {
    int r = Math.abs(v) % 1024;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntAbsModMinus1024(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            ubfx r{{\d+}}, r{{\d+}}, #0, #10
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntAbsModMinus1024(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x3ff
  private static Integer $noinline$IntAbsModMinus1024(int v) {
    int r = Math.abs(v) % -1024;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntALenMod1024(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            ubfx r{{\d+}}, r{{\d+}}, #0, #10
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntALenMod1024(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x3ff
  private static Integer $noinline$IntALenMod1024(int[] arr) {
    int r = arr.length % 1024;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntALenModMinus1024(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            ubfx r{{\d+}}, r{{\d+}}, #0, #10
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntALenModMinus1024(int[]) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x3ff
  private static Integer $noinline$IntALenModMinus1024(int[] arr) {
    int r = arr.length % -1024;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntModIntMin(int) disassembly (after)
  /// CHECK:                 asr{{s?}} r{{\d+}}, r{{\d+}}, #31
  /// CHECK-NEXT:            add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #1
  /// CHECK-NEXT:            bfc       r{{\d+}}, #0, #31
  /// CHECK-NEXT:            sub{{s?}} r{{\d+}}, r{{\d+}}, r{{\d+}}
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntModIntMin(int) disassembly (after)
  /// CHECK:                 negs w{{\d+}}, w{{\d+}}
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x7fffffff
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x7fffffff
  /// CHECK-NEXT:            csneg w{{\d+}}, w{{\d+}}, mi
  //
  /// CHECK-START-X86_64: java.lang.Integer RemTest.$noinline$IntModIntMin(int) disassembly (after)
  /// CHECK:          Rem [{{i\d+}},{{i\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shr
  /// CHECK-NOT:      imul
  /// CHECK:          mov
  /// CHECK:          and
  /// CHECK:          jz/eq
  /// CHECK:          lea
  /// CHECK:          test
  /// CHECK:          cmovl/nge
  private static Integer $noinline$IntModIntMin(int v) {
    int r = v % Integer.MIN_VALUE;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer RemTest.$noinline$IntAbsModIntMin(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            ubfx r{{\d+}}, r{{\d+}}, #0, #31
  //
  /// CHECK-START-ARM64: java.lang.Integer RemTest.$noinline$IntAbsModIntMin(int) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and w{{\d+}}, w{{\d+}}, #0x7fffffff
  private static Integer $noinline$IntAbsModIntMin(int v) {
    int r = Math.abs(v) % Integer.MIN_VALUE;
    return r;
  }

  private static void remLong() {
    expectEquals(0L, $noinline$LongMod2(0));
    expectEquals(1L, $noinline$LongMod2(1));
    expectEquals(-1L, $noinline$LongMod2(-1));
    expectEquals(0L, $noinline$LongMod2(2));
    expectEquals(0L, $noinline$LongMod2(-2));
    expectEquals(1L, $noinline$LongMod2(3));
    expectEquals(-1L, $noinline$LongMod2(-3));
    expectEquals(1L, $noinline$LongMod2(0x0f));
    expectEquals(1L, $noinline$LongMod2(0x00ff));
    expectEquals(1L, $noinline$LongMod2(0x00ffff));
    expectEquals(1L, $noinline$LongMod2(0x00ffffff));
    expectEquals(1L, $noinline$LongMod2(0x00ffffffffL));
    expectEquals(1L, $noinline$LongMod2(Long.MAX_VALUE));
    expectEquals(0L, $noinline$LongMod2(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongModMinus2(0));
    expectEquals(1L, $noinline$LongModMinus2(1));
    expectEquals(-1L, $noinline$LongModMinus2(-1));
    expectEquals(0L, $noinline$LongModMinus2(2));
    expectEquals(0L, $noinline$LongModMinus2(-2));
    expectEquals(1L, $noinline$LongModMinus2(3));
    expectEquals(-1L, $noinline$LongModMinus2(-3));
    expectEquals(1L, $noinline$LongModMinus2(0x0f));
    expectEquals(1L, $noinline$LongModMinus2(0x00ff));
    expectEquals(1L, $noinline$LongModMinus2(0x00ffff));
    expectEquals(1L, $noinline$LongModMinus2(0x00ffffff));
    expectEquals(1L, $noinline$LongModMinus2(0x00ffffffffL));
    expectEquals(1L, $noinline$LongModMinus2(Long.MAX_VALUE));
    expectEquals(0L, $noinline$LongModMinus2(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongAbsMod2(0));
    expectEquals(1L, $noinline$LongAbsMod2(1));
    expectEquals(1L, $noinline$LongAbsMod2(-1));
    expectEquals(0L, $noinline$LongAbsMod2(2));
    expectEquals(0L, $noinline$LongAbsMod2(-2));
    expectEquals(1L, $noinline$LongAbsMod2(3));
    expectEquals(1L, $noinline$LongAbsMod2(-3));
    expectEquals(1L, $noinline$LongAbsMod2(0x0f));
    expectEquals(1L, $noinline$LongAbsMod2(0x00ff));
    expectEquals(1L, $noinline$LongAbsMod2(0x00ffff));
    expectEquals(1L, $noinline$LongAbsMod2(0x00ffffff));
    expectEquals(1L, $noinline$LongAbsMod2(0x00ffffffffL));
    expectEquals(1L, $noinline$LongAbsMod2(Long.MAX_VALUE));
    expectEquals(0L, $noinline$LongAbsMod2(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongAbsModMinus2(0));
    expectEquals(1L, $noinline$LongAbsModMinus2(1));
    expectEquals(1L, $noinline$LongAbsModMinus2(-1));
    expectEquals(0L, $noinline$LongAbsModMinus2(2));
    expectEquals(0L, $noinline$LongAbsModMinus2(-2));
    expectEquals(1L, $noinline$LongAbsModMinus2(3));
    expectEquals(1L, $noinline$LongAbsModMinus2(-3));
    expectEquals(1L, $noinline$LongAbsModMinus2(0x0f));
    expectEquals(1L, $noinline$LongAbsModMinus2(0x00ff));
    expectEquals(1L, $noinline$LongAbsModMinus2(0x00ffff));
    expectEquals(1L, $noinline$LongAbsModMinus2(0x00ffffff));
    expectEquals(1L, $noinline$LongAbsModMinus2(0x00ffffffffL));
    expectEquals(1L, $noinline$LongAbsModMinus2(Long.MAX_VALUE));
    expectEquals(0L, $noinline$LongAbsModMinus2(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongMod16(0));
    expectEquals(1L, $noinline$LongMod16(1));
    expectEquals(1L, $noinline$LongMod16(17));
    expectEquals(-1L, $noinline$LongMod16(-1));
    expectEquals(0L, $noinline$LongMod16(32));
    expectEquals(0L, $noinline$LongMod16(-32));
    expectEquals(0x0fL, $noinline$LongMod16(0x0f));
    expectEquals(0x0fL, $noinline$LongMod16(0x00ff));
    expectEquals(0x0fL, $noinline$LongMod16(0x00ffff));
    expectEquals(0x0fL, $noinline$LongMod16(0x00ffffff));
    expectEquals(0x0fL, $noinline$LongMod16(0x00ffffffffL));
    expectEquals(15L, $noinline$LongMod16(Long.MAX_VALUE));
    expectEquals(0L, $noinline$LongMod16(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongModMinus16(0));
    expectEquals(1L, $noinline$LongModMinus16(1));
    expectEquals(1L, $noinline$LongModMinus16(17));
    expectEquals(-1L, $noinline$LongModMinus16(-1));
    expectEquals(0L, $noinline$LongModMinus16(32));
    expectEquals(0L, $noinline$LongModMinus16(-32));
    expectEquals(0x0fL, $noinline$LongModMinus16(0x0f));
    expectEquals(0x0fL, $noinline$LongModMinus16(0x00ff));
    expectEquals(0x0fL, $noinline$LongModMinus16(0x00ffff));
    expectEquals(0x0fL, $noinline$LongModMinus16(0x00ffffff));
    expectEquals(0x0fL, $noinline$LongModMinus16(0x00ffffffffL));
    expectEquals(15L, $noinline$LongModMinus16(Long.MAX_VALUE));
    expectEquals(0L, $noinline$LongModMinus16(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongAbsMod16(0));
    expectEquals(1L, $noinline$LongAbsMod16(1));
    expectEquals(1L, $noinline$LongAbsMod16(17));
    expectEquals(1L, $noinline$LongAbsMod16(-1));
    expectEquals(0L, $noinline$LongAbsMod16(32));
    expectEquals(0L, $noinline$LongAbsMod16(-32));
    expectEquals(0x0fL, $noinline$LongAbsMod16(0x0f));
    expectEquals(0x0fL, $noinline$LongAbsMod16(0x00ff));
    expectEquals(0x0fL, $noinline$LongAbsMod16(0x00ffff));
    expectEquals(0x0fL, $noinline$LongAbsMod16(0x00ffffff));
    expectEquals(0x0fL, $noinline$LongAbsMod16(0x00ffffffffL));
    expectEquals(15L, $noinline$LongAbsMod16(Long.MAX_VALUE));
    expectEquals(0L, $noinline$LongAbsMod16(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongAbsModMinus16(0));
    expectEquals(1L, $noinline$LongAbsModMinus16(1));
    expectEquals(1L, $noinline$LongAbsModMinus16(17));
    expectEquals(1L, $noinline$LongAbsModMinus16(-1));
    expectEquals(0L, $noinline$LongAbsModMinus16(32));
    expectEquals(0L, $noinline$LongAbsModMinus16(-32));
    expectEquals(0x0fL, $noinline$LongAbsModMinus16(0x0f));
    expectEquals(0x0fL, $noinline$LongAbsModMinus16(0x00ff));
    expectEquals(0x0fL, $noinline$LongAbsModMinus16(0x00ffff));
    expectEquals(0x0fL, $noinline$LongAbsModMinus16(0x00ffffff));
    expectEquals(0x0fL, $noinline$LongAbsModMinus16(0x00ffffffffL));
    expectEquals(15L, $noinline$LongAbsModMinus16(Long.MAX_VALUE));
    expectEquals(0L, $noinline$LongAbsModMinus16(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongModLongMin(0));
    expectEquals(1L, $noinline$LongModLongMin(1));
    expectEquals(0L, $noinline$LongModLongMin(Long.MIN_VALUE));
    expectEquals(-1L, $noinline$LongModLongMin(-1));
    expectEquals(0x0fL, $noinline$LongModLongMin(0x0f));
    expectEquals(0x00ffL, $noinline$LongModLongMin(0x00ff));
    expectEquals(0x00ffffL, $noinline$LongModLongMin(0x00ffff));
    expectEquals(0x00ffffffL, $noinline$LongModLongMin(0x00ffffff));
    expectEquals(0x00ffffffffL, $noinline$LongModLongMin(0x00ffffffffL));
    expectEquals(Long.MAX_VALUE, $noinline$LongModLongMin(Long.MAX_VALUE));

    expectEquals(0L, $noinline$LongAbsModLongMin(0));
    expectEquals(1L, $noinline$LongAbsModLongMin(1));
    expectEquals(0L, $noinline$LongAbsModLongMin(Long.MIN_VALUE));
    expectEquals(1L, $noinline$LongAbsModLongMin(-1));
    expectEquals(0x0fL, $noinline$LongAbsModLongMin(0x0f));
    expectEquals(0x00ffL, $noinline$LongAbsModLongMin(0x00ff));
    expectEquals(0x00ffffL, $noinline$LongAbsModLongMin(0x00ffff));
    expectEquals(0x00ffffffL, $noinline$LongAbsModLongMin(0x00ffffff));
    expectEquals(0x00ffffffffL, $noinline$LongAbsModLongMin(0x00ffffffffL));
    expectEquals(Long.MAX_VALUE, $noinline$LongAbsModLongMin(Long.MAX_VALUE));
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongMod2(long) disassembly (after)
  /// CHECK:                 cmp x{{\d+}}, #0x0
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0x1
  /// CHECK-NEXT:            cneg x{{\d+}}, x{{\d+}}, lt
  //
  /// CHECK-START-X86_64: java.lang.Long RemTest.$noinline$LongMod2(long) disassembly (after)
  /// CHECK:          Rem [{{j\d+}},{{j\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shrq
  /// CHECK-NOT:      imulq
  /// CHECK:          movq
  /// CHECK:          andq
  /// CHECK:          jz/eq
  /// CHECK:          movq
  /// CHECK:          sarq
  /// CHECK:          shlq
  /// CHECK:          orq
  private static Long $noinline$LongMod2(long v) {
    long r = v % 2;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongModMinus2(long) disassembly (after)
  /// CHECK:                 cmp x{{\d+}}, #0x0
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0x1
  /// CHECK-NEXT:            cneg x{{\d+}}, x{{\d+}}, lt
  //
  /// CHECK-START-X86_64: java.lang.Long RemTest.$noinline$LongModMinus2(long) disassembly (after)
  /// CHECK:          Rem [{{j\d+}},{{j\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shrq
  /// CHECK-NOT:      imulq
  /// CHECK:          movq
  /// CHECK:          andq
  /// CHECK:          jz/eq
  /// CHECK:          movq
  /// CHECK:          sarq
  /// CHECK:          shlq
  /// CHECK:          orq
  private static Long $noinline$LongModMinus2(long v) {
    long r = v % -2;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongAbsMod2(long) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0x1
  private static Long $noinline$LongAbsMod2(long v) {
    long r = Math.abs(v) % 2;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongAbsModMinus2(long) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0x1
  private static Long $noinline$LongAbsModMinus2(long v) {
    long r = Math.abs(v) % -2;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongMod16(long) disassembly (after)
  /// CHECK:                 negs x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0xf
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0xf
  /// CHECK-NEXT:            csneg x{{\d+}}, x{{\d+}}, mi

  /// CHECK-START-X86_64: java.lang.Long RemTest.$noinline$LongMod16(long) disassembly (after)
  /// CHECK:          Rem [{{j\d+}},{{j\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shrq
  /// CHECK-NOT:      imulq
  /// CHECK:          movq
  /// CHECK:          andq
  /// CHECK:          jz/eq
  /// CHECK:          movq
  /// CHECK:          sarq
  /// CHECK:          shlq
  /// CHECK:          orq
  private static Long $noinline$LongMod16(long v) {
    long r = v % 16;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongModMinus16(long) disassembly (after)
  /// CHECK:                 negs x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0xf
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0xf
  /// CHECK-NEXT:            csneg x{{\d+}}, x{{\d+}}, mi
  //
  /// CHECK-START-X86_64: java.lang.Long RemTest.$noinline$LongModMinus16(long) disassembly (after)
  /// CHECK:          Rem [{{j\d+}},{{j\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shrq
  /// CHECK-NOT:      imulq
  /// CHECK:          movq
  /// CHECK:          andq
  /// CHECK:          jz/eq
  /// CHECK:          movq
  /// CHECK:          sarq
  /// CHECK:          shlq
  /// CHECK:          orq
  private static Long $noinline$LongModMinus16(long v) {
    long r = v % -16;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongAbsMod16(long) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0xf
  private static Long $noinline$LongAbsMod16(long v) {
    long r = Math.abs(v) % 16;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongAbsModMinus16(long) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0xf
  private static Long $noinline$LongAbsModMinus16(long v) {
    long r = Math.abs(v) % -16;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongModLongMin(long) disassembly (after)
  /// CHECK:                 negs x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0x7fffffffffffffff
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0x7fffffffffffffff
  /// CHECK-NEXT:            csneg x{{\d+}}, x{{\d+}}, mi
  //
  /// CHECK-START-X86_64: java.lang.Long RemTest.$noinline$LongModLongMin(long) disassembly (after)
  /// CHECK:          Rem [{{j\d+}},{{j\d+}}]
  /// CHECK-NOT:      imul
  /// CHECK-NOT:      shrq
  /// CHECK-NOT:      imulq
  /// CHECK:          movq
  /// CHECK:          andq
  /// CHECK:          jz/eq
  /// CHECK:          movq
  /// CHECK:          sarq
  /// CHECK:          shlq
  /// CHECK:          orq
  private static Long $noinline$LongModLongMin(long v) {
    long r = v % Long.MIN_VALUE;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long RemTest.$noinline$LongAbsModLongMin(long) disassembly (after)
  /// CHECK:                 Rem
  /// CHECK-NEXT:            and x{{\d+}}, x{{\d+}}, #0x7fffffffffffffff
  private static Long $noinline$LongAbsModLongMin(long v) {
    long r = Math.abs(v) % Long.MIN_VALUE;
    return r;
  }
}
