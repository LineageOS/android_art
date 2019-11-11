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
    static public void assertTrue(boolean condition) {
        if (!condition) {
            throw new Error("condition not true");
        }
    }

    static public void assertFalse(boolean condition) {
        if (condition) {
            throw new Error("condition not false");
        }
    }

    public static void testHalfToFloatToHalfConversions(){
        // Test FP16 to float and back to Half for all possible Short values
        for (short h = Short.MIN_VALUE; h < Short.MAX_VALUE; h++) {
            if (FP16.isNaN(h)) {
                // NaN inputs are tested below.
                continue;
            }
            assertEquals(h, FP16.toHalf(FP16.toFloat(h)));
        }
    }

    public static void testToHalf(){
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

    }

    public static void testToFloat(){
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

    public static void testFloor() {
        // These tests have been taken from the cts HalfTest
        assertEquals(FP16.POSITIVE_INFINITY, FP16.floor(FP16.POSITIVE_INFINITY));
        assertEquals(FP16.NEGATIVE_INFINITY, FP16.floor(FP16.NEGATIVE_INFINITY));
        assertEquals(FP16.POSITIVE_ZERO, FP16.floor(FP16.POSITIVE_ZERO));
        assertEquals(FP16.NEGATIVE_ZERO, FP16.floor(FP16.NEGATIVE_ZERO));
        assertEquals(FP16.NaN, FP16.floor(FP16.NaN));
        assertEquals(FP16.LOWEST_VALUE, FP16.floor(FP16.LOWEST_VALUE));
        assertEquals(FP16.POSITIVE_ZERO, FP16.floor(FP16.MIN_NORMAL));
        assertEquals(FP16.POSITIVE_ZERO, FP16.floor((short) 0x3ff));
        assertEquals(FP16.POSITIVE_ZERO, FP16.floor(FP16.toHalf(0.2f)));
        assertEquals(-1.0f, FP16.toFloat(FP16.floor(FP16.toHalf(-0.2f))));
        assertEquals(-1.0f, FP16.toFloat(FP16.floor(FP16.toHalf(-0.7f))));
        assertEquals(FP16.POSITIVE_ZERO, FP16.floor(FP16.toHalf(0.7f)));
        assertEquals(124.0f, FP16.toFloat(FP16.floor(FP16.toHalf(124.7f))));
        assertEquals(-125.0f, FP16.toFloat(FP16.floor(FP16.toHalf(-124.7f))));
        assertEquals(124.0f, FP16.toFloat(FP16.floor(FP16.toHalf(124.2f))));
        assertEquals(-125.0f, FP16.toFloat(FP16.floor(FP16.toHalf(-124.2f))));
        // floor for NaN values
        assertEquals((short) 0x7e01, FP16.floor((short) 0x7c01));
        assertEquals((short) 0x7f00, FP16.floor((short) 0x7d00));
        assertEquals((short) 0xfe01, FP16.floor((short) 0xfc01));
        assertEquals((short) 0xff00, FP16.floor((short) 0xfd00));
    }

    public static void testCeil() {
        // These tests have been taken from the cts HalfTest
        assertEquals(FP16.POSITIVE_INFINITY, FP16.ceil(FP16.POSITIVE_INFINITY));
        assertEquals(FP16.NEGATIVE_INFINITY, FP16.ceil(FP16.NEGATIVE_INFINITY));
        assertEquals(FP16.POSITIVE_ZERO, FP16.ceil(FP16.POSITIVE_ZERO));
        assertEquals(FP16.NEGATIVE_ZERO, FP16.ceil(FP16.NEGATIVE_ZERO));
        assertEquals(FP16.NaN, FP16.ceil(FP16.NaN));
        assertEquals(FP16.LOWEST_VALUE, FP16.ceil(FP16.LOWEST_VALUE));
        assertEquals(1.0f, FP16.toFloat(FP16.ceil(FP16.MIN_NORMAL)));
        assertEquals(1.0f, FP16.toFloat(FP16.ceil((short) 0x3ff)));
        assertEquals(1.0f, FP16.toFloat(FP16.ceil(FP16.toHalf(0.2f))));
        assertEquals(FP16.NEGATIVE_ZERO, FP16.ceil(FP16.toHalf(-0.2f)));
        assertEquals(1.0f, FP16.toFloat(FP16.ceil(FP16.toHalf(0.7f))));
        assertEquals(FP16.NEGATIVE_ZERO, FP16.ceil(FP16.toHalf(-0.7f)));
        assertEquals(125.0f, FP16.toFloat(FP16.ceil(FP16.toHalf(124.7f))));
        assertEquals(-124.0f, FP16.toFloat(FP16.ceil(FP16.toHalf(-124.7f))));
        assertEquals(125.0f, FP16.toFloat(FP16.ceil(FP16.toHalf(124.2f))));
        assertEquals(-124.0f, FP16.toFloat(FP16.ceil(FP16.toHalf(-124.2f))));
        // ceil for NaN values
        assertEquals((short) 0x7e01, FP16.floor((short) 0x7c01));
        assertEquals((short) 0x7f00, FP16.floor((short) 0x7d00));
        assertEquals((short) 0xfe01, FP16.floor((short) 0xfc01));
        assertEquals((short) 0xff00, FP16.floor((short) 0xfd00));
    }

    public static void testRint() {
        assertEquals(FP16.POSITIVE_INFINITY, FP16.rint(FP16.POSITIVE_INFINITY));
        assertEquals(FP16.NEGATIVE_INFINITY, FP16.rint(FP16.NEGATIVE_INFINITY));
        assertEquals(FP16.POSITIVE_ZERO, FP16.rint(FP16.POSITIVE_ZERO));
        assertEquals(FP16.NEGATIVE_ZERO, FP16.rint(FP16.NEGATIVE_ZERO));
        assertEquals(FP16.NaN, FP16.rint(FP16.NaN));
        assertEquals(FP16.LOWEST_VALUE, FP16.rint(FP16.LOWEST_VALUE));
        assertEquals(FP16.POSITIVE_ZERO, FP16.rint(FP16.MIN_VALUE));
        assertEquals(FP16.POSITIVE_ZERO, FP16.rint((short) 0x200));
        assertEquals(FP16.POSITIVE_ZERO, FP16.rint((short) 0x3ff));
        assertEquals(FP16.POSITIVE_ZERO, FP16.rint(FP16.toHalf(0.2f)));
        assertEquals(FP16.NEGATIVE_ZERO, FP16.rint(FP16.toHalf(-0.2f)));
        assertEquals(1.0f, FP16.toFloat(FP16.rint(FP16.toHalf(0.7f))));
        assertEquals(-1.0f, FP16.toFloat(FP16.rint(FP16.toHalf(-0.7f))));
        assertEquals(0.0f, FP16.toFloat(FP16.rint(FP16.toHalf(0.5f))));
        assertEquals(-0.0f, FP16.toFloat(FP16.rint(FP16.toHalf(-0.5f))));
        assertEquals(125.0f, FP16.toFloat(FP16.rint(FP16.toHalf(124.7f))));
        assertEquals(-125.0f, FP16.toFloat(FP16.rint(FP16.toHalf(-124.7f))));
        assertEquals(124.0f, FP16.toFloat(FP16.rint(FP16.toHalf(124.2f))));
        assertEquals(-124.0f, FP16.toFloat(FP16.rint(FP16.toHalf(-124.2f))));
        // floor for NaN values
        assertEquals((short) 0x7e01, FP16.floor((short) 0x7c01));
        assertEquals((short) 0x7f00, FP16.floor((short) 0x7d00));
        assertEquals((short) 0xfe01, FP16.floor((short) 0xfc01));
        assertEquals((short) 0xff00, FP16.floor((short) 0xfd00));

    }

    public static void testGreater() {
        assertTrue(FP16.greater(FP16.POSITIVE_INFINITY, FP16.NEGATIVE_INFINITY));
        assertTrue(FP16.greater(FP16.POSITIVE_INFINITY, FP16.MAX_VALUE));
        assertFalse(FP16.greater(FP16.MAX_VALUE, FP16.POSITIVE_INFINITY));
        assertFalse(FP16.greater(FP16.NEGATIVE_INFINITY, FP16.LOWEST_VALUE));
        assertTrue(FP16.greater(FP16.LOWEST_VALUE, FP16.NEGATIVE_INFINITY));
        assertFalse(FP16.greater(FP16.NEGATIVE_ZERO, FP16.POSITIVE_ZERO));
        assertFalse(FP16.greater(FP16.POSITIVE_ZERO, FP16.NEGATIVE_ZERO));
        assertFalse(FP16.greater(FP16.toHalf(12.3f), FP16.NaN));
        assertFalse(FP16.greater(FP16.NaN, FP16.toHalf(12.3f)));
        assertTrue(FP16.greater(FP16.MIN_NORMAL, FP16.MIN_VALUE));
        assertFalse(FP16.greater(FP16.MIN_VALUE, FP16.MIN_NORMAL));
        assertTrue(FP16.greater(FP16.toHalf(12.4f), FP16.toHalf(12.3f)));
        assertFalse(FP16.greater(FP16.toHalf(12.3f), FP16.toHalf(12.4f)));
        assertFalse(FP16.greater(FP16.toHalf(-12.4f), FP16.toHalf(-12.3f)));
        assertTrue(FP16.greater(FP16.toHalf(-12.3f), FP16.toHalf(-12.4f)));
        assertTrue(FP16.greater((short) 0x3ff, FP16.MIN_VALUE));

        assertFalse(FP16.greater(FP16.toHalf(-1.0f), FP16.toHalf(0.0f)));
        assertTrue(FP16.greater(FP16.toHalf(0.0f), FP16.toHalf(-1.0f)));
        assertFalse(FP16.greater(FP16.toHalf(-1.0f), FP16.toHalf(-1.0f)));
        assertFalse(FP16.greater(FP16.toHalf(-1.3f), FP16.toHalf(-1.3f)));
        assertTrue(FP16.greater(FP16.toHalf(1.0f), FP16.toHalf(0.0f)));
        assertFalse(FP16.greater(FP16.toHalf(0.0f), FP16.toHalf(1.0f)));
        assertFalse(FP16.greater(FP16.toHalf(1.0f), FP16.toHalf(1.0f)));
        assertFalse(FP16.greater(FP16.toHalf(1.3f), FP16.toHalf(1.3f)));
        assertFalse(FP16.greater(FP16.toHalf(-0.1f), FP16.toHalf(0.0f)));
        assertTrue(FP16.greater(FP16.toHalf(0.0f), FP16.toHalf(-0.1f)));
        assertFalse(FP16.greater(FP16.toHalf(-0.1f), FP16.toHalf(-0.1f)));
        assertTrue(FP16.greater(FP16.toHalf(0.1f), FP16.toHalf(0.0f)));
        assertFalse(FP16.greater(FP16.toHalf(0.0f), FP16.toHalf(0.1f)));
        assertFalse(FP16.greater(FP16.toHalf(0.1f), FP16.toHalf(0.1f)));
    }

    public static void testGreaterEquals() {
        assertTrue(FP16.greaterEquals(FP16.POSITIVE_INFINITY, FP16.NEGATIVE_INFINITY));
        assertTrue(FP16.greaterEquals(FP16.POSITIVE_INFINITY, FP16.MAX_VALUE));
        assertFalse(FP16.greaterEquals(FP16.MAX_VALUE, FP16.POSITIVE_INFINITY));
        assertFalse(FP16.greaterEquals(FP16.NEGATIVE_INFINITY, FP16.LOWEST_VALUE));
        assertTrue(FP16.greaterEquals(FP16.LOWEST_VALUE, FP16.NEGATIVE_INFINITY));
        assertTrue(FP16.greaterEquals(FP16.NEGATIVE_ZERO, FP16.POSITIVE_ZERO));
        assertTrue(FP16.greaterEquals(FP16.POSITIVE_ZERO, FP16.NEGATIVE_ZERO));
        assertFalse(FP16.greaterEquals(FP16.toHalf(12.3f), FP16.NaN));
        assertFalse(FP16.greaterEquals(FP16.NaN, FP16.toHalf(12.3f)));
        assertTrue(FP16.greaterEquals(FP16.MIN_NORMAL, FP16.MIN_VALUE));
        assertFalse(FP16.greaterEquals(FP16.MIN_VALUE, FP16.MIN_NORMAL));
        assertTrue(FP16.greaterEquals(FP16.toHalf(12.4f), FP16.toHalf(12.3f)));
        assertFalse(FP16.greaterEquals(FP16.toHalf(12.3f), FP16.toHalf(12.4f)));
        assertFalse(FP16.greaterEquals(FP16.toHalf(-12.4f), FP16.toHalf(-12.3f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(-12.3f), FP16.toHalf(-12.4f)));
        assertTrue(FP16.greaterEquals((short) 0x3ff, FP16.MIN_VALUE));
        assertTrue(FP16.greaterEquals(FP16.NEGATIVE_INFINITY, FP16.NEGATIVE_INFINITY));
        assertTrue(FP16.greaterEquals(FP16.POSITIVE_INFINITY, FP16.POSITIVE_INFINITY));
        assertTrue(FP16.greaterEquals(FP16.toHalf(12.12356f), FP16.toHalf(12.12356f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(-12.12356f), FP16.toHalf(-12.12356f)));

        assertFalse(FP16.greaterEquals(FP16.toHalf(-1.0f), FP16.toHalf(0.0f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(0.0f), FP16.toHalf(-1.0f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(-1.0f), FP16.toHalf(-1.0f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(-1.3f), FP16.toHalf(-1.3f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(1.0f), FP16.toHalf(0.0f)));
        assertFalse(FP16.greaterEquals(FP16.toHalf(0.0f), FP16.toHalf(1.0f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(1.0f), FP16.toHalf(1.0f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(1.3f), FP16.toHalf(1.3f)));
        assertFalse(FP16.greaterEquals(FP16.toHalf(-0.1f), FP16.toHalf(0.0f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(0.0f), FP16.toHalf(-0.1f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(-0.1f), FP16.toHalf(-0.1f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(0.1f), FP16.toHalf(0.0f)));
        assertFalse(FP16.greaterEquals(FP16.toHalf(0.0f), FP16.toHalf(0.1f)));
        assertTrue(FP16.greaterEquals(FP16.toHalf(0.1f), FP16.toHalf(0.1f)));
    }

    public static void testLess() {
        assertTrue(FP16.less(FP16.NEGATIVE_INFINITY, FP16.POSITIVE_INFINITY));
        assertTrue(FP16.less(FP16.MAX_VALUE, FP16.POSITIVE_INFINITY));
        assertFalse(FP16.less(FP16.POSITIVE_INFINITY, FP16.MAX_VALUE));
        assertFalse(FP16.less(FP16.LOWEST_VALUE, FP16.NEGATIVE_INFINITY));
        assertTrue(FP16.less(FP16.NEGATIVE_INFINITY, FP16.LOWEST_VALUE));
        assertFalse(FP16.less(FP16.POSITIVE_ZERO, FP16.NEGATIVE_ZERO));
        assertFalse(FP16.less(FP16.NEGATIVE_ZERO, FP16.POSITIVE_ZERO));
        assertFalse(FP16.less(FP16.NaN, FP16.toHalf(12.3f)));
        assertFalse(FP16.less(FP16.toHalf(12.3f), FP16.NaN));
        assertTrue(FP16.less(FP16.MIN_VALUE, FP16.MIN_NORMAL));
        assertFalse(FP16.less(FP16.MIN_NORMAL, FP16.MIN_VALUE));
        assertTrue(FP16.less(FP16.toHalf(12.3f), FP16.toHalf(12.4f)));
        assertFalse(FP16.less(FP16.toHalf(12.4f), FP16.toHalf(12.3f)));
        assertFalse(FP16.less(FP16.toHalf(-12.3f), FP16.toHalf(-12.4f)));
        assertTrue(FP16.less(FP16.toHalf(-12.4f), FP16.toHalf(-12.3f)));
        assertTrue(FP16.less(FP16.MIN_VALUE, (short) 0x3ff));

        assertTrue(FP16.less(FP16.toHalf(-1.0f), FP16.toHalf(0.0f)));
        assertFalse(FP16.less(FP16.toHalf(0.0f), FP16.toHalf(-1.0f)));
        assertFalse(FP16.less(FP16.toHalf(-1.0f), FP16.toHalf(-1.0f)));
        assertFalse(FP16.less(FP16.toHalf(-1.3f), FP16.toHalf(-1.3f)));
        assertFalse(FP16.less(FP16.toHalf(1.0f), FP16.toHalf(0.0f)));
        assertTrue(FP16.less(FP16.toHalf(0.0f), FP16.toHalf(1.0f)));
        assertFalse(FP16.less(FP16.toHalf(1.0f), FP16.toHalf(1.0f)));
        assertFalse(FP16.less(FP16.toHalf(1.3f), FP16.toHalf(1.3f)));
        assertTrue(FP16.less(FP16.toHalf(-0.1f), FP16.toHalf(0.0f)));
        assertFalse(FP16.less(FP16.toHalf(0.0f), FP16.toHalf(-0.1f)));
        assertFalse(FP16.less(FP16.toHalf(-0.1f), FP16.toHalf(-0.1f)));
        assertFalse(FP16.less(FP16.toHalf(0.1f), FP16.toHalf(0.0f)));
        assertTrue(FP16.less(FP16.toHalf(0.0f), FP16.toHalf(0.1f)));
        assertFalse(FP16.less(FP16.toHalf(0.1f), FP16.toHalf(0.1f)));
    }

    public static void testLessEquals() {
        assertTrue(FP16.lessEquals(FP16.NEGATIVE_INFINITY, FP16.POSITIVE_INFINITY));
        assertTrue(FP16.lessEquals(FP16.MAX_VALUE, FP16.POSITIVE_INFINITY));
        assertFalse(FP16.lessEquals(FP16.POSITIVE_INFINITY, FP16.MAX_VALUE));
        assertFalse(FP16.lessEquals(FP16.LOWEST_VALUE, FP16.NEGATIVE_INFINITY));
        assertTrue(FP16.lessEquals(FP16.NEGATIVE_INFINITY, FP16.LOWEST_VALUE));
        assertTrue(FP16.lessEquals(FP16.POSITIVE_ZERO, FP16.NEGATIVE_ZERO));
        assertTrue(FP16.lessEquals(FP16.NEGATIVE_ZERO, FP16.POSITIVE_ZERO));
        assertFalse(FP16.lessEquals(FP16.NaN, FP16.toHalf(12.3f)));
        assertFalse(FP16.lessEquals(FP16.toHalf(12.3f), FP16.NaN));
        assertTrue(FP16.lessEquals(FP16.MIN_VALUE, FP16.MIN_NORMAL));
        assertFalse(FP16.lessEquals(FP16.MIN_NORMAL, FP16.MIN_VALUE));
        assertTrue(FP16.lessEquals(FP16.toHalf(12.3f), FP16.toHalf(12.4f)));
        assertFalse(FP16.lessEquals(FP16.toHalf(12.4f), FP16.toHalf(12.3f)));
        assertFalse(FP16.lessEquals(FP16.toHalf(-12.3f), FP16.toHalf(-12.4f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(-12.4f), FP16.toHalf(-12.3f)));
        assertTrue(FP16.lessEquals(FP16.MIN_VALUE, (short) 0x3ff));
        assertTrue(FP16.lessEquals(FP16.NEGATIVE_INFINITY, FP16.NEGATIVE_INFINITY));
        assertTrue(FP16.lessEquals(FP16.POSITIVE_INFINITY, FP16.POSITIVE_INFINITY));
        assertTrue(FP16.lessEquals(FP16.toHalf(12.12356f), FP16.toHalf(12.12356f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(-12.12356f), FP16.toHalf(-12.12356f)));

        assertTrue(FP16.lessEquals(FP16.toHalf(-1.0f), FP16.toHalf(0.0f)));
        assertFalse(FP16.lessEquals(FP16.toHalf(0.0f), FP16.toHalf(-1.0f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(-1.0f), FP16.toHalf(-1.0f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(-1.3f), FP16.toHalf(-1.3f)));
        assertFalse(FP16.lessEquals(FP16.toHalf(1.0f), FP16.toHalf(0.0f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(0.0f), FP16.toHalf(1.0f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(1.0f), FP16.toHalf(1.0f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(1.3f), FP16.toHalf(1.3f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(-0.1f), FP16.toHalf(0.0f)));
        assertFalse(FP16.lessEquals(FP16.toHalf(0.0f), FP16.toHalf(-0.1f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(-0.1f), FP16.toHalf(-0.1f)));
        assertFalse(FP16.lessEquals(FP16.toHalf(0.1f), FP16.toHalf(0.0f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(0.0f), FP16.toHalf(0.1f)));
        assertTrue(FP16.lessEquals(FP16.toHalf(0.1f), FP16.toHalf(0.1f)));
    }

    public static void main(String args[]) {
        testHalfToFloatToHalfConversions();
        testToHalf();
        testToFloat();
        testFloor();
        testCeil();
        testRint();
        testGreater();
        testGreaterEquals();
        testLessEquals();
        testLess();
    }
}
