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

public class DivTest {

  public static <T extends Number> void expectEquals(T expected, T result) {
    if (!expected.equals(result)) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void main() {
    divInt();
    divLong();
  }

  private static void divInt() {
    expectEquals(0, $noinline$IntDivBy2(0));
    expectEquals(0, $noinline$IntDivBy2(1));
    expectEquals(0, $noinline$IntDivBy2(-1));
    expectEquals(1, $noinline$IntDivBy2(2));
    expectEquals(-1, $noinline$IntDivBy2(-2));
    expectEquals(1, $noinline$IntDivBy2(3));
    expectEquals(-1, $noinline$IntDivBy2(-3));
    expectEquals(3, $noinline$IntDivBy2(7));
    expectEquals(-3, $noinline$IntDivBy2(-7));
    expectEquals(4, $noinline$IntDivBy2(8));
    expectEquals(-4, $noinline$IntDivBy2(-8));
    expectEquals(7, $noinline$IntDivBy2(0x0f));
    expectEquals(0x007f, $noinline$IntDivBy2(0x00ff));
    expectEquals(0x07ff, $noinline$IntDivBy2(0x0fff));
    expectEquals(0x007fff, $noinline$IntDivBy2(0x00ffff));
    expectEquals(0x3fffffff, $noinline$IntDivBy2(Integer.MAX_VALUE));
    expectEquals(0xc0000000, $noinline$IntDivBy2(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntDivByMinus2(0));
    expectEquals(0, $noinline$IntDivByMinus2(1));
    expectEquals(0, $noinline$IntDivByMinus2(-1));
    expectEquals(-1, $noinline$IntDivByMinus2(2));
    expectEquals(1, $noinline$IntDivByMinus2(-2));
    expectEquals(-1, $noinline$IntDivByMinus2(3));
    expectEquals(1, $noinline$IntDivByMinus2(-3));
    expectEquals(-3, $noinline$IntDivByMinus2(7));
    expectEquals(3, $noinline$IntDivByMinus2(-7));
    expectEquals(-4, $noinline$IntDivByMinus2(8));
    expectEquals(4, $noinline$IntDivByMinus2(-8));
    expectEquals(-7, $noinline$IntDivByMinus2(0x0f));
    expectEquals(0xffffff81, $noinline$IntDivByMinus2(0x00ff));
    expectEquals(0xfffff801, $noinline$IntDivByMinus2(0x0fff));
    expectEquals(0xffff8001, $noinline$IntDivByMinus2(0x00ffff));
    expectEquals(0xc0000001, $noinline$IntDivByMinus2(Integer.MAX_VALUE));
    expectEquals(0x40000000, $noinline$IntDivByMinus2(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsDivBy2(0));
    expectEquals(0, $noinline$IntAbsDivBy2(1));
    expectEquals(0, $noinline$IntAbsDivBy2(-1));
    expectEquals(1, $noinline$IntAbsDivBy2(2));
    expectEquals(1, $noinline$IntAbsDivBy2(-2));
    expectEquals(1, $noinline$IntAbsDivBy2(3));
    expectEquals(1, $noinline$IntAbsDivBy2(-3));
    expectEquals(3, $noinline$IntAbsDivBy2(7));
    expectEquals(3, $noinline$IntAbsDivBy2(-7));
    expectEquals(4, $noinline$IntAbsDivBy2(8));
    expectEquals(4, $noinline$IntAbsDivBy2(-8));
    expectEquals(7, $noinline$IntAbsDivBy2(0x0f));
    expectEquals(0x007f, $noinline$IntAbsDivBy2(0x00ff));
    expectEquals(0x07ff, $noinline$IntAbsDivBy2(0x0fff));
    expectEquals(0x007fff, $noinline$IntAbsDivBy2(0x00ffff));
    expectEquals(0x3fffffff, $noinline$IntAbsDivBy2(Integer.MAX_VALUE));
    expectEquals(0xc0000000, $noinline$IntAbsDivBy2(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsDivByMinus2(0));
    expectEquals(0, $noinline$IntAbsDivByMinus2(1));
    expectEquals(0, $noinline$IntAbsDivByMinus2(-1));
    expectEquals(-1, $noinline$IntAbsDivByMinus2(2));
    expectEquals(-1, $noinline$IntAbsDivByMinus2(-2));
    expectEquals(-1, $noinline$IntAbsDivByMinus2(3));
    expectEquals(-1, $noinline$IntAbsDivByMinus2(-3));
    expectEquals(-3, $noinline$IntAbsDivByMinus2(7));
    expectEquals(-3, $noinline$IntAbsDivByMinus2(-7));
    expectEquals(-4, $noinline$IntAbsDivByMinus2(8));
    expectEquals(-4, $noinline$IntAbsDivByMinus2(-8));
    expectEquals(-7, $noinline$IntAbsDivByMinus2(0x0f));
    expectEquals(0xffffff81, $noinline$IntAbsDivByMinus2(0x00ff));
    expectEquals(0xfffff801, $noinline$IntAbsDivByMinus2(0x0fff));
    expectEquals(0xffff8001, $noinline$IntAbsDivByMinus2(0x00ffff));
    expectEquals(0xc0000001, $noinline$IntAbsDivByMinus2(Integer.MAX_VALUE));
    expectEquals(0x40000000, $noinline$IntAbsDivByMinus2(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntALenDivBy2(new int[0]));
    expectEquals(0, $noinline$IntALenDivBy2(new int[1]));
    expectEquals(1, $noinline$IntALenDivBy2(new int[2]));
    expectEquals(1, $noinline$IntALenDivBy2(new int[3]));
    expectEquals(3, $noinline$IntALenDivBy2(new int[7]));
    expectEquals(4, $noinline$IntALenDivBy2(new int[8]));
    expectEquals(7, $noinline$IntALenDivBy2(new int[0x0f]));
    expectEquals(0x007f, $noinline$IntALenDivBy2(new int[0x00ff]));
    expectEquals(0x07ff, $noinline$IntALenDivBy2(new int[0x0fff]));
    expectEquals(0x007fff, $noinline$IntALenDivBy2(new int[0x00ffff]));

    expectEquals(0, $noinline$IntALenDivByMinus2(new int[0]));
    expectEquals(0, $noinline$IntALenDivByMinus2(new int[1]));
    expectEquals(-1, $noinline$IntALenDivByMinus2(new int[2]));
    expectEquals(-1, $noinline$IntALenDivByMinus2(new int[3]));
    expectEquals(-3, $noinline$IntALenDivByMinus2(new int[7]));
    expectEquals(-4, $noinline$IntALenDivByMinus2(new int[8]));
    expectEquals(-7, $noinline$IntALenDivByMinus2(new int[0x0f]));
    expectEquals(0xffffff81, $noinline$IntALenDivByMinus2(new int[0x00ff]));
    expectEquals(0xfffff801, $noinline$IntALenDivByMinus2(new int[0x0fff]));
    expectEquals(0xffff8001, $noinline$IntALenDivByMinus2(new int[0x00ffff]));

    expectEquals(0, $noinline$IntDivBy16(0));
    expectEquals(1, $noinline$IntDivBy16(16));
    expectEquals(-1, $noinline$IntDivBy16(-16));
    expectEquals(2, $noinline$IntDivBy16(33));
    expectEquals(0x000f, $noinline$IntDivBy16(0x00ff));
    expectEquals(0x00ff, $noinline$IntDivBy16(0x0fff));
    expectEquals(0x000fff, $noinline$IntDivBy16(0x00ffff));
    expectEquals(0x07ffffff, $noinline$IntDivBy16(Integer.MAX_VALUE));
    expectEquals(0xf8000000, $noinline$IntDivBy16(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntDivByMinus16(0));
    expectEquals(-1, $noinline$IntDivByMinus16(16));
    expectEquals(1, $noinline$IntDivByMinus16(-16));
    expectEquals(-2, $noinline$IntDivByMinus16(33));
    expectEquals(0xfffffff1, $noinline$IntDivByMinus16(0x00ff));
    expectEquals(0xffffff01, $noinline$IntDivByMinus16(0x0fff));
    expectEquals(0xfffff001, $noinline$IntDivByMinus16(0x00ffff));
    expectEquals(0xf8000001, $noinline$IntDivByMinus16(Integer.MAX_VALUE));
    expectEquals(0x08000000, $noinline$IntDivByMinus16(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsDivBy16(0));
    expectEquals(1, $noinline$IntAbsDivBy16(16));
    expectEquals(1, $noinline$IntAbsDivBy16(-16));
    expectEquals(2, $noinline$IntAbsDivBy16(33));
    expectEquals(0x000f, $noinline$IntAbsDivBy16(0x00ff));
    expectEquals(0x00ff, $noinline$IntAbsDivBy16(0x0fff));
    expectEquals(0x000fff, $noinline$IntAbsDivBy16(0x00ffff));
    expectEquals(0x07ffffff, $noinline$IntAbsDivBy16(Integer.MAX_VALUE));
    expectEquals(0xf8000000, $noinline$IntAbsDivBy16(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntAbsDivByMinus16(0));
    expectEquals(-1, $noinline$IntAbsDivByMinus16(16));
    expectEquals(-1, $noinline$IntAbsDivByMinus16(-16));
    expectEquals(-2, $noinline$IntAbsDivByMinus16(33));
    expectEquals(0xfffffff1, $noinline$IntAbsDivByMinus16(0x00ff));
    expectEquals(0xffffff01, $noinline$IntAbsDivByMinus16(0x0fff));
    expectEquals(0xfffff001, $noinline$IntAbsDivByMinus16(0x00ffff));
    expectEquals(0xf8000001, $noinline$IntAbsDivByMinus16(Integer.MAX_VALUE));
    expectEquals(0x08000000, $noinline$IntAbsDivByMinus16(Integer.MIN_VALUE));

    expectEquals(0, $noinline$IntALenDivBy16(new int[0]));
    expectEquals(1, $noinline$IntALenDivBy16(new int[16]));
    expectEquals(2, $noinline$IntALenDivBy16(new int[33]));
    expectEquals(0x000f, $noinline$IntALenDivBy16(new int[0x00ff]));
    expectEquals(0x00ff, $noinline$IntALenDivBy16(new int[0x0fff]));
    expectEquals(0x000fff, $noinline$IntALenDivBy16(new int[0x00ffff]));

    expectEquals(0, $noinline$IntALenDivByMinus16(new int[0]));
    expectEquals(-1, $noinline$IntALenDivByMinus16(new int[16]));
    expectEquals(-2, $noinline$IntALenDivByMinus16(new int[33]));
    expectEquals(0xfffffff1, $noinline$IntALenDivByMinus16(new int[0x00ff]));
    expectEquals(0xffffff01, $noinline$IntALenDivByMinus16(new int[0x0fff]));
    expectEquals(0xfffff001, $noinline$IntALenDivByMinus16(new int[0x00ffff]));

    expectEquals(0, $noinline$IntDivByIntMin(0));
    expectEquals(0, $noinline$IntDivByIntMin(1));
    expectEquals(0, $noinline$IntDivByIntMin(-1));
    expectEquals(1, $noinline$IntDivByIntMin(Integer.MIN_VALUE));
    expectEquals(0, $noinline$IntDivByIntMin(Integer.MAX_VALUE));

    expectEquals(0, $noinline$IntAbsDivByIntMin(0));
    expectEquals(0, $noinline$IntAbsDivByIntMin(1));
    expectEquals(0, $noinline$IntAbsDivByIntMin(-1));
    expectEquals(1, $noinline$IntAbsDivByIntMin(Integer.MIN_VALUE));
    expectEquals(0, $noinline$IntAbsDivByIntMin(Integer.MAX_VALUE));
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntDivBy2(int) disassembly (after)
  /// CHECK:                 add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #31
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #1
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntDivBy2(int) disassembly (after)
  /// CHECK:                 add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK-NEXT:            asr w{{\d+}}, w{{\d+}}, #1
  //
  /// CHECK-START-X86_64: java.lang.Integer DivTest.$noinline$IntDivBy2(int) disassembly (after)
  /// CHECK-NOT:             cmovnl/geq
  /// CHECK:                 add
  private static Integer $noinline$IntDivBy2(int v) {
    int r = v / 2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntDivByMinus2(int) disassembly (after)
  /// CHECK:                 add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #31
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #1
  /// CHECK-NEXT:            rsb{{s?}} r{{\d+}}, #0
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntDivByMinus2(int) disassembly (after)
  /// CHECK:                 add w{{\d+}}, w{{\d+}}, w{{\d+}}, lsr #31
  /// CHECK-NEXT:            neg w{{\d+}}, w{{\d+}}, asr #1
  //
  /// CHECK-START-X86_64: java.lang.Integer DivTest.$noinline$IntDivByMinus2(int) disassembly (after)
  /// CHECK-NOT:             cmovnl/geq
  /// CHECK:                 add
  private static Integer $noinline$IntDivByMinus2(int v) {
    int r = v / -2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntAbsDivBy2(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #1
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntAbsDivBy2(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr w{{\d+}}, w{{\d+}}, #1
  private static Integer $noinline$IntAbsDivBy2(int v) {
    int r = Math.abs(v) / 2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntAbsDivByMinus2(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #1
  /// CHECK-NEXT:            rsb{{s?}} r{{\d+}}, #0
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntAbsDivByMinus2(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            neg w{{\d+}}, w{{\d+}}, asr #1
  private static Integer $noinline$IntAbsDivByMinus2(int v) {
    int r = Math.abs(v) / -2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntALenDivBy2(int[]) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #1
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntALenDivBy2(int[]) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr w{{\d+}}, w{{\d+}}, #1
  private static Integer $noinline$IntALenDivBy2(int[] arr) {
    int r = arr.length / 2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntALenDivByMinus2(int[]) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #1
  /// CHECK-NEXT:            rsb{{s?}} r{{\d+}}, #0
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntALenDivByMinus2(int[]) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            neg w{{\d+}}, w{{\d+}}, asr #1
  private static Integer $noinline$IntALenDivByMinus2(int[] arr) {
    int r = arr.length / -2;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntDivBy16(int) disassembly (after)
  /// CHECK:                 asr{{s?}} r{{\d+}}, r{{\d+}}, #31
  /// CHECK-NEXT:            add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #28
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #4
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntDivBy16(int) disassembly (after)
  /// CHECK:                 add w{{\d+}}, w{{\d+}}, #0xf
  /// CHECK-NEXT:            cmp w{{\d+}}, #0x0
  /// CHECK-NEXT:            csel w{{\d+}}, w{{\d+}}, w{{\d+}}, lt
  /// CHECK-NEXT:            asr w{{\d+}}, w{{\d+}}, #4
  private static Integer $noinline$IntDivBy16(int v) {
    int r = v / 16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntDivByMinus16(int) disassembly (after)
  /// CHECK:                 asr{{s?}} r{{\d+}}, r{{\d+}}, #31
  /// CHECK-NEXT:            add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #28
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #4
  /// CHECK-NEXT:            rsb{{s?}} r{{\d+}}, #0
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntDivByMinus16(int) disassembly (after)
  /// CHECK:                 add w{{\d+}}, w{{\d+}}, #0xf
  /// CHECK-NEXT:            cmp w{{\d+}}, #0x0
  /// CHECK-NEXT:            csel w{{\d+}}, w{{\d+}}, w{{\d+}}, lt
  /// CHECK-NEXT:            neg w{{\d+}}, w{{\d+}}, asr #4
  private static Integer $noinline$IntDivByMinus16(int v) {
    int r = v / -16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntAbsDivBy16(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #4
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntAbsDivBy16(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr w{{\d+}}, w{{\d+}}, #4
  private static Integer $noinline$IntAbsDivBy16(int v) {
    int r = Math.abs(v) / 16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntAbsDivByMinus16(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #4
  /// CHECK-NEXT:            rsb{{s?}} r{{\d+}}, #0
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntAbsDivByMinus16(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            neg w{{\d+}}, w{{\d+}}, asr #4
  private static Integer $noinline$IntAbsDivByMinus16(int v) {
    int r = Math.abs(v) / -16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntALenDivBy16(int[]) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #4
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntALenDivBy16(int[]) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr w{{\d+}}, w{{\d+}}, #4
  private static Integer $noinline$IntALenDivBy16(int[] arr) {
    int r = arr.length / 16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntALenDivByMinus16(int[]) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #4
  /// CHECK-NEXT:            rsb{{s?}} r{{\d+}}, #0
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntALenDivByMinus16(int[]) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            neg w{{\d+}}, w{{\d+}}, asr #4
  private static Integer $noinline$IntALenDivByMinus16(int[] arr) {
    int r = arr.length / -16;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntDivByIntMin(int) disassembly (after)
  /// CHECK:                 asr{{s?}} r{{\d+}}, r{{\d+}}, #31
  /// CHECK-NEXT:            add       r{{\d+}}, r{{\d+}}, r{{\d+}}, lsr #1
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #31
  /// CHECK-NEXT:            rsb{{s?}} r{{\d+}}, #0
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntDivByIntMin(int) disassembly (after)
  /// CHECK:                 mov w{{\d+}}, #0x7fffffff
  /// CHECK-NEXT:            add w{{\d+}}, w{{\d+}}, w{{\d+}}
  /// CHECK-NEXT:            cmp w{{\d+}}, #0x0
  /// CHECK-NEXT:            csel w{{\d+}}, w{{\d+}}, w{{\d+}}, lt
  /// CHECK-NEXT:            neg w{{\d+}}, w{{\d+}}, asr #31
  private static Integer $noinline$IntDivByIntMin(int v) {
    int r = v / Integer.MIN_VALUE;
    return r;
  }

  /// CHECK-START-ARM:   java.lang.Integer DivTest.$noinline$IntAbsDivByIntMin(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr{{s?}} r{{\d+}}, #31
  /// CHECK-NEXT:            rsb{{s?}} r{{\d+}}, #0
  //
  /// CHECK-START-ARM64: java.lang.Integer DivTest.$noinline$IntAbsDivByIntMin(int) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            neg w{{\d+}}, w{{\d+}}, asr #31
  private static Integer $noinline$IntAbsDivByIntMin(int v) {
    int r = Math.abs(v) / Integer.MIN_VALUE;
    return r;
  }

  private static void divLong() {
    expectEquals(0L, $noinline$LongDivBy2(0L));
    expectEquals(0L, $noinline$LongDivBy2(1L));
    expectEquals(0L, $noinline$LongDivBy2(-1L));
    expectEquals(1L, $noinline$LongDivBy2(2L));
    expectEquals(-1L, $noinline$LongDivBy2(-2L));
    expectEquals(1L, $noinline$LongDivBy2(3L));
    expectEquals(-1L, $noinline$LongDivBy2(-3L));
    expectEquals(3L, $noinline$LongDivBy2(7L));
    expectEquals(-3L, $noinline$LongDivBy2(-7L));
    expectEquals(4L, $noinline$LongDivBy2(8L));
    expectEquals(-4L, $noinline$LongDivBy2(-8L));
    expectEquals(7L, $noinline$LongDivBy2(0x0fL));
    expectEquals(0x007fL, $noinline$LongDivBy2(0x00ffL));
    expectEquals(0x07ffL, $noinline$LongDivBy2(0x0fffL));
    expectEquals(0x007fffL, $noinline$LongDivBy2(0x00ffffL));
    expectEquals(0x3fffffffffffffffL, $noinline$LongDivBy2(Long.MAX_VALUE));
    expectEquals(0xc000000000000000L, $noinline$LongDivBy2(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongDivByMinus2(0));
    expectEquals(0L, $noinline$LongDivByMinus2(1L));
    expectEquals(0L, $noinline$LongDivByMinus2(-1L));
    expectEquals(-1L, $noinline$LongDivByMinus2(2L));
    expectEquals(1L, $noinline$LongDivByMinus2(-2L));
    expectEquals(-1L, $noinline$LongDivByMinus2(3L));
    expectEquals(1L, $noinline$LongDivByMinus2(-3L));
    expectEquals(-3L, $noinline$LongDivByMinus2(7L));
    expectEquals(3L, $noinline$LongDivByMinus2(-7L));
    expectEquals(-4L, $noinline$LongDivByMinus2(8L));
    expectEquals(4L, $noinline$LongDivByMinus2(-8L));
    expectEquals(-7L, $noinline$LongDivByMinus2(0x0fL));
    expectEquals(0xffffffffffffff81L, $noinline$LongDivByMinus2(0x00ffL));
    expectEquals(0xfffffffffffff801L, $noinline$LongDivByMinus2(0x0fffL));
    expectEquals(0xffffffffffff8001L, $noinline$LongDivByMinus2(0x00ffffL));
    expectEquals(0xc000000000000001L, $noinline$LongDivByMinus2(Long.MAX_VALUE));
    expectEquals(0x4000000000000000L, $noinline$LongDivByMinus2(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongAbsDivBy2(0L));
    expectEquals(0L, $noinline$LongAbsDivBy2(1L));
    expectEquals(0L, $noinline$LongAbsDivBy2(-1L));
    expectEquals(1L, $noinline$LongAbsDivBy2(2L));
    expectEquals(1L, $noinline$LongAbsDivBy2(-2L));
    expectEquals(1L, $noinline$LongAbsDivBy2(3L));
    expectEquals(1L, $noinline$LongAbsDivBy2(-3L));
    expectEquals(3L, $noinline$LongAbsDivBy2(7L));
    expectEquals(3L, $noinline$LongAbsDivBy2(-7L));
    expectEquals(4L, $noinline$LongAbsDivBy2(8L));
    expectEquals(4L, $noinline$LongAbsDivBy2(-8L));
    expectEquals(7L, $noinline$LongAbsDivBy2(0x0fL));
    expectEquals(0x007fL, $noinline$LongAbsDivBy2(0x00ffL));
    expectEquals(0x07ffL, $noinline$LongAbsDivBy2(0x0fffL));
    expectEquals(0x007fffL, $noinline$LongAbsDivBy2(0x00ffffL));
    expectEquals(0x3fffffffffffffffL, $noinline$LongAbsDivBy2(Long.MAX_VALUE));
    expectEquals(0xc000000000000000L, $noinline$LongAbsDivBy2(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongAbsDivByMinus2(0));
    expectEquals(0L, $noinline$LongAbsDivByMinus2(1L));
    expectEquals(0L, $noinline$LongAbsDivByMinus2(-1L));
    expectEquals(-1L, $noinline$LongAbsDivByMinus2(2L));
    expectEquals(-1L, $noinline$LongAbsDivByMinus2(-2L));
    expectEquals(-1L, $noinline$LongAbsDivByMinus2(3L));
    expectEquals(-1L, $noinline$LongAbsDivByMinus2(-3L));
    expectEquals(-3L, $noinline$LongAbsDivByMinus2(7L));
    expectEquals(-3L, $noinline$LongAbsDivByMinus2(-7L));
    expectEquals(-4L, $noinline$LongAbsDivByMinus2(8L));
    expectEquals(-4L, $noinline$LongAbsDivByMinus2(-8L));
    expectEquals(-7L, $noinline$LongAbsDivByMinus2(0x0fL));
    expectEquals(0xffffffffffffff81L, $noinline$LongAbsDivByMinus2(0x00ffL));
    expectEquals(0xfffffffffffff801L, $noinline$LongAbsDivByMinus2(0x0fffL));
    expectEquals(0xffffffffffff8001L, $noinline$LongAbsDivByMinus2(0x00ffffL));
    expectEquals(0xc000000000000001L, $noinline$LongAbsDivByMinus2(Long.MAX_VALUE));
    expectEquals(0x4000000000000000L, $noinline$LongAbsDivByMinus2(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongDivBy16(0));
    expectEquals(1L, $noinline$LongDivBy16(16L));
    expectEquals(-1L, $noinline$LongDivBy16(-16L));
    expectEquals(2L, $noinline$LongDivBy16(33L));
    expectEquals(0x000fL, $noinline$LongDivBy16(0x00ffL));
    expectEquals(0x00ffL, $noinline$LongDivBy16(0x0fffL));
    expectEquals(0x000fffL, $noinline$LongDivBy16(0x00ffffL));
    expectEquals(0x07ffffffffffffffL, $noinline$LongDivBy16(Long.MAX_VALUE));
    expectEquals(0xf800000000000000L, $noinline$LongDivBy16(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongDivByMinus16(0));
    expectEquals(-1L, $noinline$LongDivByMinus16(16L));
    expectEquals(1L, $noinline$LongDivByMinus16(-16L));
    expectEquals(-2L, $noinline$LongDivByMinus16(33L));
    expectEquals(0xfffffffffffffff1L, $noinline$LongDivByMinus16(0x00ffL));
    expectEquals(0xffffffffffffff01L, $noinline$LongDivByMinus16(0x0fffL));
    expectEquals(0xfffffffffffff001L, $noinline$LongDivByMinus16(0x00ffffL));
    expectEquals(0xf800000000000001L, $noinline$LongDivByMinus16(Long.MAX_VALUE));
    expectEquals(0x0800000000000000L, $noinline$LongDivByMinus16(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongAbsDivBy16(0));
    expectEquals(1L, $noinline$LongAbsDivBy16(16L));
    expectEquals(1L, $noinline$LongAbsDivBy16(-16L));
    expectEquals(2L, $noinline$LongAbsDivBy16(33L));
    expectEquals(0x000fL, $noinline$LongAbsDivBy16(0x00ffL));
    expectEquals(0x00ffL, $noinline$LongAbsDivBy16(0x0fffL));
    expectEquals(0x000fffL, $noinline$LongAbsDivBy16(0x00ffffL));
    expectEquals(0x07ffffffffffffffL, $noinline$LongAbsDivBy16(Long.MAX_VALUE));
    expectEquals(0xf800000000000000L, $noinline$LongAbsDivBy16(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongAbsDivByMinus16(0));
    expectEquals(-1L, $noinline$LongAbsDivByMinus16(16L));
    expectEquals(-1L, $noinline$LongAbsDivByMinus16(-16L));
    expectEquals(-2L, $noinline$LongAbsDivByMinus16(33L));
    expectEquals(0xfffffffffffffff1L, $noinline$LongAbsDivByMinus16(0x00ffL));
    expectEquals(0xffffffffffffff01L, $noinline$LongAbsDivByMinus16(0x0fffL));
    expectEquals(0xfffffffffffff001L, $noinline$LongAbsDivByMinus16(0x00ffffL));
    expectEquals(0xf800000000000001L, $noinline$LongAbsDivByMinus16(Long.MAX_VALUE));
    expectEquals(0x0800000000000000L, $noinline$LongAbsDivByMinus16(Long.MIN_VALUE));

    expectEquals(0L, $noinline$LongDivByLongMin(0));
    expectEquals(0L, $noinline$LongDivByLongMin(1));
    expectEquals(0L, $noinline$LongDivByLongMin(-1));
    expectEquals(1L, $noinline$LongDivByLongMin(Long.MIN_VALUE));
    expectEquals(0L, $noinline$LongDivByLongMin(Long.MAX_VALUE));

    expectEquals(0L, $noinline$LongAbsDivByLongMin(0));
    expectEquals(0L, $noinline$LongAbsDivByLongMin(1));
    expectEquals(0L, $noinline$LongAbsDivByLongMin(-1));
    expectEquals(1L, $noinline$LongAbsDivByLongMin(Long.MIN_VALUE));
    expectEquals(0L, $noinline$LongAbsDivByLongMin(Long.MAX_VALUE));
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongDivBy2(long) disassembly (after)
  /// CHECK:                 add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            asr x{{\d+}}, x{{\d+}}, #1
  //
  /// CHECK-START-X86_64: java.lang.Long DivTest.$noinline$LongDivBy2(long) disassembly (after)
  /// CHECK-NOT:             cmovnl/geq
  /// CHECK:                 addq
  private static Long $noinline$LongDivBy2(long v) {
    long r = v / 2;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongDivByMinus2(long) disassembly (after)
  /// CHECK:                 add x{{\d+}}, x{{\d+}}, x{{\d+}}, lsr #63
  /// CHECK-NEXT:            neg x{{\d+}}, x{{\d+}}, asr #1
  //
  /// CHECK-START-X86_64: java.lang.Long DivTest.$noinline$LongDivByMinus2(long) disassembly (after)
  /// CHECK-NOT:             cmovnl/geq
  /// CHECK:                 addq
  private static Long $noinline$LongDivByMinus2(long v) {
    long r = v / -2;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongAbsDivBy2(long) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr x{{\d+}}, x{{\d+}}, #1
  private static Long $noinline$LongAbsDivBy2(long v) {
    long r = Math.abs(v) / 2;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongAbsDivByMinus2(long) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            neg x{{\d+}}, x{{\d+}}, asr #1
  private static Long $noinline$LongAbsDivByMinus2(long v) {
    long r = Math.abs(v) / -2;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongDivBy16(long) disassembly (after)
  /// CHECK:                 add x{{\d+}}, x{{\d+}}, #0xf
  /// CHECK-NEXT:            cmp x{{\d+}}, #0x0
  /// CHECK-NEXT:            csel x{{\d+}}, x{{\d+}}, x{{\d+}}, lt
  /// CHECK-NEXT:            asr x{{\d+}}, x{{\d+}}, #4
  private static Long $noinline$LongDivBy16(long v) {
    long r = v / 16;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongDivByMinus16(long) disassembly (after)
  /// CHECK:                 add x{{\d+}}, x{{\d+}}, #0xf
  /// CHECK-NEXT:            cmp x{{\d+}}, #0x0
  /// CHECK-NEXT:            csel x{{\d+}}, x{{\d+}}, x{{\d+}}, lt
  /// CHECK-NEXT:            neg x{{\d+}}, x{{\d+}}, asr #4
  private static Long $noinline$LongDivByMinus16(long v) {
    long r = v / -16;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongAbsDivBy16(long) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            asr x{{\d+}}, x{{\d+}}, #4
  private static Long $noinline$LongAbsDivBy16(long v) {
    long r = Math.abs(v) / 16;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongAbsDivByMinus16(long) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            neg x{{\d+}}, x{{\d+}}, asr #4
  private static Long $noinline$LongAbsDivByMinus16(long v) {
    long r = Math.abs(v) / -16;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongDivByLongMin(long) disassembly (after)
  /// CHECK:                 mov x{{\d+}}, #0x7fffffffffffffff
  /// CHECK-NEXT:            add x{{\d+}}, x{{\d+}}, x{{\d+}}
  /// CHECK-NEXT:            cmp x{{\d+}}, #0x0
  /// CHECK-NEXT:            csel x{{\d+}}, x{{\d+}}, x{{\d+}}, lt
  /// CHECK-NEXT:            neg x{{\d+}}, x{{\d+}}, asr #63
  private static Long $noinline$LongDivByLongMin(long v) {
    long r = v / Long.MIN_VALUE;
    return r;
  }

  /// CHECK-START-ARM64: java.lang.Long DivTest.$noinline$LongAbsDivByLongMin(long) disassembly (after)
  /// CHECK:                 Div
  /// CHECK-NEXT:            neg x{{\d+}}, x{{\d+}}, asr #63
  private static Long $noinline$LongAbsDivByLongMin(long v) {
    long r = Math.abs(v) / Long.MIN_VALUE;
    return r;
  }
}
