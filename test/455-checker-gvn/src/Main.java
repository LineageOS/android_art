/*
 * Copyright (C) 2014 The Android Open Source Project
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
import java.lang.reflect.Method;

public class Main {

  private static int mX = 2;
  private static int mY = -3;

  public static void main(String[] args) {
    System.out.println($noinline$foo(3, 4));
    System.out.println($noinline$mulAndIntrinsic());
    System.out.println($noinline$directIntrinsic(-5));
  }

  private static int $inline$add(int a, int b) {
    return a + b;
  }

  /// CHECK-START: int Main.$noinline$foo(int, int) GVN (before)
  /// CHECK: Add
  /// CHECK: Add
  /// CHECK: Add

  /// CHECK-START: int Main.$noinline$foo(int, int) GVN (after)
  /// CHECK: Add
  /// CHECK: Add
  /// CHECK-NOT: Add
  public static int $noinline$foo(int x, int y) {
    int sum1 = $inline$add(x, y);
    int sum2 = $inline$add(y, x);
    return sum1 + sum2;
  }

  /// CHECK-START: int Main.$noinline$mulAndIntrinsic() GVN (before)
  /// CHECK: StaticFieldGet
  /// CHECK: StaticFieldGet
  /// CHECK: Mul
  /// CHECK: Abs
  /// CHECK: StaticFieldGet
  /// CHECK: StaticFieldGet
  /// CHECK: Mul
  /// CHECK: Add

  /// CHECK-START: int Main.$noinline$mulAndIntrinsic() GVN (after)
  /// CHECK: StaticFieldGet
  /// CHECK: StaticFieldGet
  /// CHECK: Mul
  /// CHECK: Abs
  /// CHECK-NOT: StaticFieldGet
  /// CHECK-NOT: StaticFieldGet
  /// CHECK-NOT: Mul
  /// CHECK: Add

  public static int $noinline$mulAndIntrinsic() {
    // The intermediate call to abs() does not kill
    // the common subexpression on the multiplication.
    int mul1 = mX * mY;
    int abs  = Math.abs(mul1);
    int mul2 = mY * mX;
    return abs + mul2;
  }

  /// CHECK-START: int Main.$noinline$directIntrinsic(int) GVN (before)
  /// CHECK: Abs
  /// CHECK: Abs
  /// CHECK: Add

  /// CHECK-START: int Main.$noinline$directIntrinsic(int) GVN (after)
  /// CHECK: Abs
  /// CHECK-NOT: Abs
  /// CHECK: Add

  public static int $noinline$directIntrinsic(int x) {
    // Here, the two calls to abs() themselves can be replaced with just one.
    int abs1 = Math.abs(x);
    int abs2 = Math.abs(x);
    return abs1 + abs2;
  }
}
