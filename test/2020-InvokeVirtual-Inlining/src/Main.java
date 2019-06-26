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


    public static void main(String[] args) {
        Test test = new Test();
        long workJ = 2;
        long workK = 3;
        float workJ1 = 10.0f;
        float workK1 = 15.0f;
        int workJ2 = 10;
        int workK2 = 15;
        long workJ3 = 0xFAEFFFAB;
        long workK3 = 0xF8E9DCBA;

        for (long i = 0; i < iterations; i++) {
            workJ = test.simplemethodMul(workJ, workK) + i;
        }
        assertLongEquals(workJ, 132855);

        for (float i = 0.0f; i < iterations; i++) {
            workJ1 = test.simplemethodRem(workJ1, workK1) + i;
        }
        assertFloatEquals(workJ1, 14.0f);

        workJ2--;

        try {
            throw new Exception("Test");
        } catch (Exception e) {
            workJ++;
        }

        for (int i = 0; i < iterations; i++) {
            workJ2 = test.simplemethodInt(workJ2, workK2) + i;
        }
        assertIntEquals(workJ2, 152);

        for (long i = 0; i < iterations; i++) {
            workJ3 = test.simplemethodXor(workJ3, workK3) + i;
        }
        assertLongEquals(workJ3, 118891342);
    }
}
