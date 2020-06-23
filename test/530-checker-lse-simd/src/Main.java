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

public class Main {
  // Based on Linpack.matgen
  // Load-store elimination did not work when a function had SIMD code.
  // In the test below loop B is vectorized.
  // Check that a redundant ArrayGet is eliminated in loop A.

  /// CHECK-START: double Main.$noinline$vecgen(double[], double[], int) load_store_elimination (before)
  /// CHECK:      Rem
  /// CHECK-NEXT: TypeConversion
  /// CHECK-NEXT: Sub
  /// CHECK-NEXT: Mul
  /// CHECK-NEXT: ArraySet
  /// CHECK-NEXT: ArrayGet
  /// CHECK-NEXT: LessThanOrEqual
  /// CHECK-NEXT: Select
  /// CHECK-NEXT: Add
  /// CHECK-NEXT: Goto loop:{{B\d+}}

  /// CHECK-START: double Main.$noinline$vecgen(double[], double[], int) load_store_elimination (after)
  /// CHECK:      Rem
  /// CHECK-NEXT: TypeConversion
  /// CHECK-NEXT: Sub
  /// CHECK-NEXT: Mul
  /// CHECK-NEXT: ArraySet
  /// CHECK-NEXT: LessThanOrEqual
  /// CHECK-NEXT: Select
  /// CHECK-NEXT: Add
  /// CHECK-NEXT: Goto loop:{{B\d+}}
  static double $noinline$vecgen(double a[], double b[], int n) {
    double norma = 0.0;
    int init = 1325;
    // Loop A
    for (int i = 0; i < n; ++i) {
      init = 3125*init % 65536;
      a[i] = (init - 32768.0)/16384.0;
      norma = (a[i] > norma) ? a[i] : norma; // ArrayGet should be removed by LSE.
    }

    // Loop B
    for (int i = 0; i < n; ++i) {
      b[i] += a[i];
    }

    return norma;
  }


  static void test01() {
    double a[] = new double[1024];
    double norma = $noinline$vecgen(a, a, a.length);
    System.out.println((int)norma);
    System.out.println((int)a[1023]);
  }

  // Check LSE works when a function has SIMD code.
  //
  /// CHECK-START: double Main.$noinline$test02(double[], int) load_store_elimination (before)
  /// CHECK:      BoundsCheck loop:none
  /// CHECK-NEXT: ArrayGet
  /// CHECK-NEXT: Mul
  /// CHECK-NEXT: ArraySet
  /// CHECK-NEXT: ArrayGet
  /// CHECK-NEXT: ArrayLength
  /// CHECK-NEXT: BelowOrEqual
  //
  /// CHECK:      ArrayGet loop:none
  /// CHECK-NEXT: Return

  /// CHECK-START: double Main.$noinline$test02(double[], int) load_store_elimination (after)
  /// CHECK:      BoundsCheck loop:none
  /// CHECK-NEXT: ArrayGet
  /// CHECK-NEXT: Mul
  /// CHECK-NEXT: ArraySet
  /// CHECK-NEXT: ArrayLength
  /// CHECK-NEXT: BelowOrEqual
  //
  /// CHECK:      Return
  static double $noinline$test02(double a[], int n) {
    double b[] = new double[n];
    a[0] = a[0] / 2;

    double norma = a[0]; // ArrayGet should be removed by LSE.

    // The following loop is vectorized.
    for (int i = 0; i < 128; ++i) {
      b[i] += a[i];
    }

    norma = a[0]; // ArrayGet should be removed by LSE.
    return norma;
  }

  static void test02() {
    double a[] = new double[128];
    java.util.Arrays.fill(a, 2.0);
    double norma = $noinline$test02(a, a.length);
    System.out.println((int)norma);
  }

  // Check LSE works when a function has SIMD code.
  //
  /// CHECK-START: double Main.$noinline$test03(int) load_store_elimination (before)
  /// CHECK:      ArrayGet loop:none
  /// CHECK-NEXT: Return

  /// CHECK-START: double Main.$noinline$test03(int) load_store_elimination (after)
  /// CHECK-NOT:  ArrayGet loop:none
  static double $noinline$test03(int n) {
    double a[] = new double[n];
    double b[] = new double[n];

    a[0] = 2.0;

    // The following loop is vectorized.
    for (int i = 0; i < 128; ++i) {
      b[i] += a[i];
    }

    a[0] = 2.0;
    return a[0]; // ArrayGet should be removed by LSE.
  }

  static void test03() {
    double norma = $noinline$test03(128);
    System.out.println((int)norma);
  }

  // Check LSE eliminates VecLoad.
  //
  /// CHECK-START-ARM64: double[] Main.$noinline$test04(int) load_store_elimination (before)
  /// CHECK:             VecStore
  /// CHECK-NEXT:        VecLoad
  /// CHECK-NEXT:        VecAdd
  /// CHECK-NEXT:        VecStore
  /// CHECK-NEXT:        Add
  /// CHECK-NEXT:        Goto loop:{{B\d+}}

  /// CHECK-START-ARM64: double[] Main.$noinline$test04(int) load_store_elimination (after)
  /// CHECK:             VecStore
  /// CHECK-NEXT:        VecAdd
  /// CHECK-NEXT:        VecStore
  /// CHECK-NEXT:        Add
  /// CHECK-NEXT:        Goto loop:{{B\d+}}
  static double[] $noinline$test04(int n) {
    double a[] = new double[n];
    double b[] = new double[n];

    // The following loop is vectorized.
    for (int i = 0; i < n; ++i) {
      a[i] = 1;
      b[i] = a[i] + a[i]; // VecLoad should be removed by LSE.
    }

    return b;
  }

  static void test04() {
    double norma = $noinline$test04(128)[0];
    System.out.println((int)norma);
  }

  // Check LSE eliminates VecLoad.
  //
  /// CHECK-START-ARM64: double[] Main.$noinline$test05(int) load_store_elimination (before)
  /// CHECK:             VecStore
  /// CHECK-NEXT:        VecLoad
  /// CHECK-NEXT:        VecStore
  /// CHECK-NEXT:        VecStore
  /// CHECK-NEXT:        Add
  /// CHECK-NEXT:        Goto loop:{{B\d+}}

  /// CHECK-START-ARM64: double[] Main.$noinline$test05(int) load_store_elimination (after)
  /// CHECK:             VecStore
  /// CHECK-NEXT:        VecStore
  /// CHECK-NEXT:        Add
  /// CHECK-NEXT:        Goto loop:{{B\d+}}
  static double[] $noinline$test05(int n) {
    double a[] = new double[n];
    double b[] = new double[n];

    // The following loop is vectorized.
    for (int i = 0; i < n; ++i) {
      a[i] = 1;
      b[i] = a[i];
      a[i] = 1;
    }

    return b;
  }

  static void test05() {
    double norma = $noinline$test05(128)[0];
    System.out.println((int)norma);
  }

  // Check LSE eliminates VecLoad and ArrayGet in case of singletons and default values.
  //
  /// CHECK-START-ARM64: double[] Main.$noinline$test06(int) load_store_elimination (before)
  /// CHECK:             BoundsCheck loop:none
  /// CHECK-NEXT:        ArrayGet
  /// CHECK-NEXT:        Add
  /// CHECK-NEXT:        ArrayLength
  //
  /// CHECK:             VecLoad loop:{{B\d+}}
  /// CHECK-NEXT:        VecStore
  /// CHECK-NEXT:        VecLoad
  /// CHECK-NEXT:        VecLoad
  /// CHECK-NEXT:        VecAdd
  /// CHECK-NEXT:        VecAdd
  /// CHECK-NEXT:        VecStore

  /// CHECK-START-ARM64: double[] Main.$noinline$test06(int) load_store_elimination (after)
  /// CHECK:             BoundsCheck loop:none
  /// CHECK-NEXT:        Add
  /// CHECK-NEXT:        ArrayLength
  //
  /// CHECK:             VecLoad loop:{{B\d+}}
  /// CHECK-NEXT:        VecStore
  /// CHECK-NEXT:        VecAdd
  /// CHECK-NEXT:        VecAdd
  /// CHECK-NEXT:        VecStore
  static double[] $noinline$test06(int n) {
    double a[] = new double[n];
    double b[] = new double[n];

    double r = a[0] + 1.0; // ArrayGet:a[0] is eliminated and default 0.0 is used.
    // The following loop is vectorized.
    for (int i = 0; i < n; ++i) {
      b[i] = a[i]; // VecLoad:a[i] is not eliminated.
      b[i] += a[i] + r; // VecLoad:a[i] and VecLoad:b[i] are eliminated.
    }

    return b;
  }

  static void test06() {
    double norma = $noinline$test06(128)[0];
    System.out.println((int)norma);
  }

  public static void main(String[] args) {
    test01();
    test02();
    test03();
    test04();
    test05();
    test06();
  }
}

