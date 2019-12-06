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

package other;

/**
 * Tests for dot product idiom vectorization: char and short case.
 */
public class TestFloatDouble {

  public static final int ARRAY_SIZE = 1024;


  /// CHECK-START-{X86_64}: float other.TestFloatDouble.testDotProdSimpleFloat(float[], float[]) loop_optimization (after)
  /// CHECK-NOT:                 VecDotProd
  public static final float testDotProdSimpleFloat(float[] a, float[] b) {
    float sum = 0;
    for (int i = 0; i < b.length; i++) {
      sum += a[i] * b[i];
    }
    return sum;
  }


  /// CHECK-START-{X86_64}: double other.TestFloatDouble.testDotProdSimpleDouble(double[], double[]) loop_optimization (after)
  /// CHECK-NOT:                 VecDotProd

  public static final double testDotProdSimpleDouble(double[] a, double[] b) {
    double sum = 0;
    for (int i = 0; i < b.length; i++) {
      sum += a[i] * b[i];
    }
    return sum;
  }

  private static void expectEquals(float expected, float result) {
    if (Float.compare(expected, result) != 0) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectEquals(double expected, double result) {
    if (Double.compare(expected, result) != 0) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void run() {
    final float MAX_F = Float.MAX_VALUE;
    final float MIN_F = Float.MIN_VALUE;
    final double MAX_D = Double.MAX_VALUE;
    final double MIN_D = Double.MIN_VALUE;

    double[] a = new double[1024];
    for (int i = 0; i != 1024; ++i) a[i] = MAX_D;
    double[] b = new double[1024];
    for (int i = 0; i != 1024; ++i) b[i] = ((i & 1) == 0) ? 1.0 : -1.0;
    expectEquals(0.0, testDotProdSimpleDouble(a,b));

    float[] f1_1 = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3.33f, 0.125f, 3.0f, 0.25f};
    float[] f2_1 = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6.125f, 2.25f, 1.213f, 0.5f};
    expectEquals(24.4415f, testDotProdSimpleFloat(f1_1, f2_1));

    float [] f1_2 = { 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0,  0.63671875f, 0.76953125f, 0.22265625f, 1.0f};
    float [] f2_2 = { 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, MIN_F, MAX_F, MAX_F, MIN_F };
    expectEquals(3.376239E38f, testDotProdSimpleFloat(f1_2, f2_2));

    float[] f1_3 = { 0xc0000000, 0xc015c28f, 0x411dd42c, 0, 0, 0, 0,
                     0, 0, 0, 0, 0, 0, 0, MIN_F, MIN_F };
    float[] f2_3 = { 0x3f4c779a, 0x408820c5, 0, 0, 0, 0, 0,
                     0, 0, 0, 0, 0, 0x00000000, 0, MAX_F, MAX_F };
    expectEquals(-2.30124471E18f, testDotProdSimpleFloat(f1_3, f2_3));
  }

  public static void main(String[] args) {
    run();
  }
}
