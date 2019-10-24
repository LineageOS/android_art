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

    public static void assertEquals(short expected, short calculated) {
        if (expected != calculated) {
            throw new Error("Expected: " + expected + ", Calculated: " + calculated);
        }
    }
    public static void assertEquals(float expected, float calculated) {
        if (expected != calculated) {
            throw new Error("Expected: " + expected + ", Calculated: " + calculated);
        }
    }

    public static void main(String args[]) {
        // Test FP16 to float
        for (short h = Short.MIN_VALUE; h < Short.MAX_VALUE; h++) {
            if (FP16.isNaN(h)) {
                // NaN inputs are tested below.
                continue;
            }
            assertEquals(h, FP16.toHalf(FP16.toFloat(h)));
        }

        // These asserts check some known values and edge cases for FP16.toHalf
        // and have been inspired by the cts HalfTest.
        // Zeroes, NaN and infinities
        assertEquals(FP16.POSITIVE_ZERO, FP16.toHalf(0.0f));
        assertEquals(FP16.NEGATIVE_ZERO, FP16.toHalf(-0.0f));
        assertEquals(FP16.NaN, FP16.toHalf(Float.NaN));
        assertEquals(FP16.POSITIVE_INFINITY, FP16.toHalf(Float.POSITIVE_INFINITY));
        assertEquals(FP16.NEGATIVE_INFINITY, FP16.toHalf(Float.NEGATIVE_INFINITY));
        // Known values
        assertEquals((short) 0x3c01, FP16.toHalf(1.0009765625f));
        assertEquals((short) 0xc000, FP16.toHalf(-2.0f));
        assertEquals((short) 0x0400, FP16.toHalf(6.10352e-5f));
        assertEquals((short) 0x7bff, FP16.toHalf(65504.0f));
        assertEquals((short) 0x3555, FP16.toHalf(1.0f / 3.0f));
        // Subnormals
        assertEquals((short) 0x03ff, FP16.toHalf(6.09756e-5f));
        assertEquals(FP16.MIN_VALUE, FP16.toHalf(5.96046e-8f));
        assertEquals((short) 0x83ff, FP16.toHalf(-6.09756e-5f));
        assertEquals((short) 0x8001, FP16.toHalf(-5.96046e-8f));
        // Subnormals (flushed to +/-0)
        assertEquals(FP16.POSITIVE_ZERO, FP16.toHalf(5.96046e-9f));
        assertEquals(FP16.NEGATIVE_ZERO, FP16.toHalf(-5.96046e-9f));
        // Test for values that overflow the mantissa bits into exp bits
        assertEquals(0x1000, FP16.toHalf(Float.intBitsToFloat(0x39fff000)));
        assertEquals(0x0400, FP16.toHalf(Float.intBitsToFloat(0x387fe000)));
        // Floats with absolute value above +/-65519 are rounded to +/-inf
        // when using round-to-even
        assertEquals(0x7bff, FP16.toHalf(65519.0f));
        assertEquals(0x7bff, FP16.toHalf(65519.9f));
        assertEquals(FP16.POSITIVE_INFINITY, FP16.toHalf(65520.0f));
        assertEquals(FP16.NEGATIVE_INFINITY, FP16.toHalf(-65520.0f));
        // Check if numbers are rounded to nearest even when they
        // cannot be accurately represented by Half
        assertEquals(0x6800, FP16.toHalf(2049.0f));
        assertEquals(0x6c00, FP16.toHalf(4098.0f));
        assertEquals(0x7000, FP16.toHalf(8196.0f));
        assertEquals(0x7400, FP16.toHalf(16392.0f));
        assertEquals(0x7800, FP16.toHalf(32784.0f));

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
