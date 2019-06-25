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

class Main {
    final static int iterations = 10;

    public static void assertIntEquals(int expected, int result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }

    public static void assertDoubleEquals(double expected, double result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }

    public static void assertFloatEquals(float expected, float result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }

    public static void assertLongEquals(long expected, long result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }


    public static long simpleMethod(long jj, long kk) {
        jj = jj >>> kk;
        return jj;
    }
    public static int simpleMethod1(int jj, int kk) {
        jj = jj << kk;
        jj = jj << kk;
        return jj;
      }
    public static float simpleMethod2(float jj, float ii) {
        jj = ii / jj;
        jj = jj / ii;
        return jj;
    }

    public static void main(String[] args) {
        long workJ = 0xFFEFAAAA;
        long workK = 0xF8E9BBBB;
        int workJ1 = 0xFFEF;
        int workK1 = 0xF8E9;
        float workJ2 = 10.0f;
        float workK2 = 15.0f;



        for (long i = 0; i < iterations; i++) {
            workJ = simpleMethod(workJ, workK) + i;
        }
        assertLongEquals(workJ, 9);

        for (int i = 0; i < iterations; i++) {
            workJ1 = simpleMethod1(workJ1, workK1) + i;
        }
        assertIntEquals(workJ1, 2097161);

        for (float i = 0.0f; i < iterations; i++) {
            workJ2 = simpleMethod2(workJ2, workK2) + i;
        }
        assertFloatEquals(workJ2, 9.122855f);
    }
}
