/*
 * Copyright 2019 The Android Open Source Project
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
    public static void main(String[] args) throws Exception {
        Class<?> c = Class.forName("B146014745");
        Method m1 = c.getDeclaredMethod("$noinline$testAppend1", String.class, int.class);
        String b146014745_result1 = (String) m1.invoke(null, "x", 42);
        assertEquals("x42x42", b146014745_result1);
        Method m2 = c.getDeclaredMethod("$noinline$testAppend2", String.class, int.class);
        String b146014745_result2 = (String) m2.invoke(null, "x", 42);
        assertEquals("x42x42", b146014745_result2);
        Method m3 = c.getDeclaredMethod("$noinline$testAppend3", String.class, int.class);
        String b146014745_result3 = (String) m3.invoke(null, "x", 42);
        assertEquals("x42x42", b146014745_result3);

        System.out.println("passed");
    }

    public static void assertEquals(String expected, String actual) {
        if (!expected.equals(actual)) {
            throw new AssertionError("Expected: " + expected + ", actual: " + actual);
        }
    }
}
