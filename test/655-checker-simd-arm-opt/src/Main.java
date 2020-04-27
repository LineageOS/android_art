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
 * Checker test for arm and arm64 simd optimizations.
 */
public class Main {

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  /// CHECK-START-ARM64: void Main.encodableConstants(byte[], short[], char[], int[], long[], float[], double[]) disassembly (after)
  /// CHECK-DAG: <<C1:i\d+>>   IntConstant 1
  /// CHECK-DAG: <<C2:i\d+>>   IntConstant -128
  /// CHECK-DAG: <<C3:i\d+>>   IntConstant 127
  /// CHECK-DAG: <<C4:i\d+>>   IntConstant -219
  /// CHECK-DAG: <<C5:i\d+>>   IntConstant 219
  /// CHECK-DAG: <<L6:j\d+>>   LongConstant 219
  /// CHECK-DAG: <<F7:f\d+>>   FloatConstant 2
  /// CHECK-DAG: <<F8:f\d+>>   FloatConstant 14.34
  /// CHECK-DAG: <<D9:d\d+>>   DoubleConstant 20
  /// CHECK-DAG: <<D10:d\d+>>  DoubleConstant 0
  //
  /// CHECK-DAG:               VecReplicateScalar [<<C1>>]
  /// CHECK-DAG:               VecReplicateScalar [<<C2>>]
  /// CHECK-DAG:               VecReplicateScalar [<<C3>>]
  /// CHECK-DAG:               VecReplicateScalar [<<C4>>]
  /// CHECK-DAG:               VecReplicateScalar [<<C5>>]
  /// CHECK-DAG:               VecReplicateScalar [<<L6>>]
  /// CHECK-DAG:               VecReplicateScalar [<<F7>>]
  /// CHECK-DAG:               VecReplicateScalar [<<F8>>]
  /// CHECK-DAG:               VecReplicateScalar [<<D9>>]
  /// CHECK-DAG:               VecReplicateScalar [<<D10>>]
  private static void encodableConstants(byte[] b, short[] s, char[] c, int[] a, long[] l, float[] f, double[] d) {
    for (int i = 0; i < ARRAY_SIZE; i++) {
      b[i] += 1;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      s[i] += -128;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      c[i] += 127;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      a[i] += -219;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      a[i] += 219;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      l[i] += 219;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      f[i] += 2.0f;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      f[i] += 14.34f;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      d[i] += 20.0;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
      d[i] += 0.0;
    }
  }

  private static int sumArray(byte[] b, short[] s, char[] c, int[] a, long[] l, float[] f, double[] d) {
    int sum = 0;
    for (int i = 0; i < ARRAY_SIZE; i++) {
      sum += b[i] + s[i] + c[i] + a[i] + l[i] + f[i] + d[i];
    }
    return sum;
  }

  public static final int ARRAY_SIZE = 128;

  public static void main(String[] args) {
    byte[] b = new byte[ARRAY_SIZE];
    short[] s = new short[ARRAY_SIZE];
    char[] c = new char[ARRAY_SIZE];
    int[] a = new int[ARRAY_SIZE];
    long[] l = new long[ARRAY_SIZE];
    float[] f = new float[ARRAY_SIZE];
    double[] d = new double[ARRAY_SIZE];

    encodableConstants(b, s, c, a, l, f, d);
    expectEquals(32640, sumArray(b, s, c, a, l, f, d));

    System.out.println("passed");
  }
}
