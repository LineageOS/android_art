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

import annotations.BootstrapMethod;
import annotations.CalledByIndy;
import annotations.Constant;
import java.lang.invoke.CallSite;
import java.lang.invoke.ConstantCallSite;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

// Tests for methods generating invoke-custom/range.
public class TestLinkerMethodWithRange extends TestBase {
    @CalledByIndy(
        bootstrapMethod =
                @BootstrapMethod(
                    enclosingType = TestLinkerMethodWithRange.class,
                    name = "primLinkerMethod",
                    parameterTypes = {
                        MethodHandles.Lookup.class,
                        String.class,
                        MethodType.class,
                        int.class,
                        int.class,
                        int.class,
                        int.class,
                        int.class,
                        float.class,
                        double.class,
                        String.class,
                        Class.class,
                        long.class
                    }
                ),
        fieldOrMethodName = "_add",
        returnType = int.class,
        parameterTypes = {int.class, int.class, int.class, int.class, int.class, int.class},
        constantArgumentsForBootstrapMethod = {
            @Constant(intValue = -1),
            @Constant(intValue = 1),
            @Constant(intValue = (int) 'a'),
            @Constant(intValue = 1024),
            @Constant(intValue = 1),
            @Constant(floatValue = 11.1f),
            @Constant(doubleValue = 2.2),
            @Constant(stringValue = "Hello"),
            @Constant(classValue = TestLinkerMethodWithRange.class),
            @Constant(longValue = 123456789L)
        }
    )

    private static int add(int a, int b, int c, int d, int e, int f) {
        assertNotReached();
        return -1;
    }

    @SuppressWarnings("unused")
    private static int _add(int a, int b, int c, int d, int e, int f) {
        return a + b + c + d + e + f;
    }

    @SuppressWarnings("unused")
    private static CallSite primLinkerMethod(
            MethodHandles.Lookup caller,
            String name,
            MethodType methodType,
            int v1,
            int v2,
            int v3,
            int v4,
            int v5,
            float v6,
            double v7,
            String v8,
            Class<?> v9,
            long v10)
            throws Throwable {
        System.out.println("Linking " + name + " " + methodType);
        assertEquals(-1, v1);
        assertEquals(1, v2);
        assertEquals('a', v3);
        assertEquals(1024, v4);
        assertEquals(1, v5);
        assertEquals(11.1f, v6);
        assertEquals(2.2, v7);
        assertEquals("Hello", v8);
        assertEquals(TestLinkerMethodWithRange.class, v9);
        assertEquals(123456789L, v10);
        MethodHandle mh_add =
                caller.findStatic(TestLinkerMethodWithRange.class, name, methodType);
        return new ConstantCallSite(mh_add);
    }

    public static void test(int u, int v, int w, int x, int y, int z) throws Throwable {
        assertEquals(u + v + w + x + y + z, add(u, v, w, x, y, z));
        System.out.println(u + v + w + x + y + z);
    }

    @CalledByIndy(
        bootstrapMethod =
                @BootstrapMethod(
                    enclosingType = TestLinkerMethodWithRange.class,
                    name = "refLinkerMethod",
                    parameterTypes = {
                        MethodHandles.Lookup.class,
                        String.class,
                        MethodType.class,
                    }
                ),
        fieldOrMethodName = "_multiply",
        returnType = Integer.class,
        parameterTypes = {Double.class, Double.class, Double.class,
                          Double.class, Double.class, Double.class},
        constantArgumentsForBootstrapMethod = {}
    )

    private static Double multiply(Double a, Double b, Double c, Double d, Double e, Double f) {
        assertNotReached();
        return 0.0;
    }

    @SuppressWarnings("unused")
    private static Double _multiply(Double a, Double b, Double c, Double d, Double e, Double f) {
        Double[] values = new Double[] { a, b, c, d, e, f };
        Double product = 1.0;
        for (Double value : values) {
            if (value != null) {
                product *= value;
            }
        }
        return product;
    }

    @SuppressWarnings("unused")
    private static CallSite refLinkerMethod(
            MethodHandles.Lookup caller, String name, MethodType methodType) throws Throwable {
        System.out.println("Linking " + name + " " + methodType);
        MethodHandle mh_multiply =
                caller.findStatic(TestLinkerMethodWithRange.class, name, methodType);
        return new ConstantCallSite(mh_multiply);
    }

    public static void test(Double u, Double v, Double w, Double x, Double y, Double z)
            throws Throwable {
        Double product = 1.0;
        if (u != null) product *= u;
        if (v != null) product *= v;
        if (w != null) product *= w;
        if (x != null) product *= x;
        if (y != null) product *= y;
        if (z != null) product *= z;
        assertEquals(product, multiply(u, v, w, x, y, z));
        System.out.println(product);
    }
}
