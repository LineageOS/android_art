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
 * Test corner cases for loop vectorizer.
 */
public class Main {
  /// CHECK-START: void Main.$noinline$testDivZeroCheck() loop_optimization (before)
  /// CHECK: DivZeroCheck
  /// CHECK-NOT: DivZeroCheck
  /// CHECK-START: void Main.$noinline$testDivZeroCheck() loop_optimization (after)
  /// CHECK: DivZeroCheck
  public static void $noinline$testDivZeroCheck() {
    int[] a = new int[10];
    for (int i = 0; i < a.length; ++i) {
      int x = 42 / 0;  // unused but throwing
      a[i] = 42;
    }
  }

  static class Base {}
  static class Foo extends Base {}
  static class Bar extends Base {}

  /// CHECK-START: void Main.$noinline$testCheckCast() loop_optimization (before)
  /// CHECK: CheckCast
  /// CHECK-NOT: CheckCast
  /// CHECK-START: void Main.$noinline$testCheckCast() loop_optimization (after)
  /// CHECK: CheckCast
  public static void $noinline$testCheckCast() {
    Base base = new Foo();
    int[] a = new int[10];
    for (int i = 0; i < a.length; ++i) {
      Bar bar = (Bar) base;  // unused but throwing
      a[i] = 42;
    }
  }

  /// CHECK-START: void Main.$noinline$testBoundsCheck() loop_optimization (before)
  /// CHECK: BoundsCheck
  /// CHECK-NOT: BoundsCheck
  /// CHECK-START: void Main.$noinline$testBoundsCheck() loop_optimization (after)
  /// CHECK: BoundsCheck
  public static void $noinline$testBoundsCheck() {
    int[] a = new int[10];
    for (int i = 0; i < a.length; ++i) {
      int x = a[11];  // unused but throwing
      a[i] = 42;
    }
  }

  public static void main(String[] args) {
    // We must not optimize any of the exceptions away.
    try {
      $noinline$testDivZeroCheck();
    } catch (java.lang.ArithmeticException e) {
      System.out.println("DivZeroCheck");
    }
    try {
      $noinline$testCheckCast();
    } catch (java.lang.ClassCastException e) {
      System.out.println("CheckCast");
    }
    try {
      $noinline$testBoundsCheck();
    } catch (java.lang.ArrayIndexOutOfBoundsException e) {
      System.out.println("BoundsCheck");
    }
  }
}
