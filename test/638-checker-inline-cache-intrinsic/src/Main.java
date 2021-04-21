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

public class Main {

  /// CHECK-START: char Main.$noinline$inlineMonomorphic(java.lang.CharSequence) inliner (before)
  /// CHECK:       InvokeInterface method_name:java.lang.CharSequence.charAt

  /// CHECK-START: char Main.$noinline$inlineMonomorphic(java.lang.CharSequence) inliner (after)
  /// CHECK:       Deoptimize
  /// CHECK:       InvokeVirtual method_name:java.lang.String.charAt intrinsic:StringCharAt

  /// CHECK-START: char Main.$noinline$inlineMonomorphic(java.lang.CharSequence) instruction_simplifier$after_inlining (after)
  /// CHECK:       Deoptimize
  /// CHECK-NOT:   InvokeInterface
  /// CHECK-NOT:   InvokeVirtual

  public static char $noinline$inlineMonomorphic(CharSequence cs) {
    return cs.charAt(0);
  }

  /// CHECK-START: char Main.$noinline$inlinePolymorphic(java.lang.CharSequence) inliner (before)
  /// CHECK:       InvokeInterface method_name:java.lang.CharSequence.charAt

  /// CHECK-START: char Main.$noinline$inlinePolymorphic(java.lang.CharSequence) inliner (after)
  /// CHECK:       InvokeVirtual method_name:java.lang.String.charAt intrinsic:StringCharAt
  /// CHECK:       Deoptimize

  /// CHECK-START: char Main.$noinline$inlinePolymorphic(java.lang.CharSequence) instruction_simplifier$after_inlining (after)
  /// CHECK:       Deoptimize

  /// CHECK-START: char Main.$noinline$inlinePolymorphic(java.lang.CharSequence) instruction_simplifier$after_inlining (after)
  /// CHECK-NOT:   InvokeInterface

  /// CHECK-START: char Main.$noinline$inlinePolymorphic(java.lang.CharSequence) instruction_simplifier$after_inlining (after)
  /// CHECK-NOT:   InvokeVirtual method_name:java.lang.String.charAt

  public static char $noinline$inlinePolymorphic(CharSequence cs) {
    return cs.charAt(0);
  }

  /// CHECK-START: char Main.$noinline$knownReceiverType() inliner (before)
  /// CHECK:       InvokeInterface method_name:java.lang.CharSequence.charAt

  /// CHECK-START: char Main.$noinline$knownReceiverType() inliner (after)
  /// CHECK:       InvokeVirtual method_name:java.lang.String.charAt intrinsic:StringCharAt

  /// CHECK-START: char Main.$noinline$knownReceiverType() instruction_simplifier$after_inlining (after)
  /// CHECK-NOT:   InvokeInterface
  /// CHECK-NOT:   InvokeVirtual

  public static char $noinline$knownReceiverType() {
    CharSequence cs = "abc";
    return cs.charAt(1);
  }

  /// CHECK-START: boolean Main.$noinline$stringEquals(java.lang.Object) inliner (before)
  /// CHECK:       InvokeVirtual method_name:java.lang.Object.equals intrinsic:None

  /// CHECK-START: boolean Main.$noinline$stringEquals(java.lang.Object) inliner (after)
  /// CHECK:       Deoptimize
  /// CHECK:       InvokeVirtual method_name:java.lang.String.equals intrinsic:StringEquals

  /// CHECK-START: boolean Main.$noinline$stringEquals(java.lang.Object) instruction_simplifier$after_inlining (after)
  /// CHECK:       Deoptimize
  /// CHECK:       InvokeVirtual method_name:java.lang.String.equals intrinsic:StringEquals

  public static boolean $noinline$stringEquals(Object obj) {
    return obj.equals("def");
  }

  public static void test() {
    ensureJitBaselineCompiled(Main.class, "$noinline$stringEquals");
    ensureJitBaselineCompiled(Main.class, "$noinline$inlineMonomorphic");
    ensureJitBaselineCompiled(Main.class, "$noinline$knownReceiverType");
    ensureJitBaselineCompiled(Main.class, "$noinline$inlinePolymorphic");
    // Warm up inline cache.
    for (int i = 0; i < 600000; i++) {
      $noinline$inlineMonomorphic(str);
      $noinline$stringEquals(str);
      $noinline$inlinePolymorphic(str);
      $noinline$inlinePolymorphic(strBuilder);
    }
    ensureJitCompiled(Main.class, "$noinline$stringEquals");
    ensureJitCompiled(Main.class, "$noinline$inlineMonomorphic");
    ensureJitCompiled(Main.class, "$noinline$inlinePolymorphic");
    ensureJitCompiled(Main.class, "$noinline$knownReceiverType");
    if ($noinline$inlineMonomorphic(str) != 'x') {
      throw new Error("Expected x");
    }
    if ($noinline$inlinePolymorphic(str) != 'x') {
      throw new Error("Expected x");
    }
    if ($noinline$inlinePolymorphic(strBuilder) != 'a') {
      throw new Error("Expected a");
    }
    if ($noinline$knownReceiverType() != 'b') {
      throw new Error("Expected b");
    }
    if ($noinline$stringEquals("abc")) {
      throw new Error("Expected false");
    }
  }

  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    test();
  }

  static String str = "xyz";
  static StringBuilder strBuilder = new StringBuilder("abc");

  private static native void ensureJitBaselineCompiled(Class<?> itf, String method_name);
  private static native void ensureJitCompiled(Class<?> itf, String method_name);
}
