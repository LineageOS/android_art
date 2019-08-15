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

import libcore.util.FP16;

public class Main {
    public Main() {
    }

    public static int TestFP16ToFloatRawIntBits(short half) {
        float f = FP16.toFloat(half);
        // Since in this test class we need to check the integer representing of
        // the actual float NaN values, the floatToRawIntBits() is used instead of
        // floatToIntBits().
        return Float.floatToRawIntBits(f);
    }

    public static void assertEquals(int expected, int actual) {
        if (expected != actual) {
            throw new Error("Expected: " + expected + ", found: " + actual);
        }
    }

    public static void assertEquals(float expected, float actual) {
        if (expected != actual) {
            throw new Error("Expected: " + expected + ", found: " + actual);
        }
    }

    public static void main(String args[]) {
        // Test FP16 to float
        for (short h = Short.MIN_VALUE; h < Short.MAX_VALUE; h++) {
            if (FP16.isNaN(h)) {
                // NaN inputs are tested below.
                continue;
            }
            assertEquals(FP16.toHalf(FP16.toFloat(h)), h);
        }
        // FP16 SNaN/QNaN inputs to float
        // The most significant bit of mantissa:
        //                 V
        // 0xfc01: 1 11111 0000000001 (signaling NaN)
        // 0xfdff: 1 11111 0111111111 (signaling NaN)
        // 0xfe00: 1 11111 1000000000 (quiet NaN)
        // 0xffff: 1 11111 1111111111 (quiet NaN)
        // This test is inspired by Java implementation of android.util.Half.toFloat(),
        // where the implementation performs SNaN->QNaN conversion.
        assert(Float.isNaN(FP16.toFloat((short)0xfc01)));
        assert(Float.isNaN(FP16.toFloat((short)0xfdff)));
        assert(Float.isNaN(FP16.toFloat((short)0xfe00)));
        assert(Float.isNaN(FP16.toFloat((short)0xffff)));
        assertEquals(0xffc02000, TestFP16ToFloatRawIntBits((short)(0xfc01)));  // SNaN->QNaN
        assertEquals(0xffffe000, TestFP16ToFloatRawIntBits((short)(0xfdff)));  // SNaN->QNaN
        assertEquals(0xffc00000, TestFP16ToFloatRawIntBits((short)(0xfe00)));  // QNaN->QNaN
        assertEquals(0xffffe000, TestFP16ToFloatRawIntBits((short)(0xffff)));  // QNaN->QNaN
    }
}
