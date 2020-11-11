/*
 * Copyright (C) 2020 The Android Open Source Project
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

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

public class VarHandleFpCasTests {
    public static class FieldFloatTest extends VarHandleUnitTest {
        private static final VarHandle vh;

        public static class A {
            public float field;
        }

        static {
            try {
                vh = MethodHandles.lookup().findVarHandle(A.class, "field", float.class);
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }

        @Override
        protected void doTest() {
            A a = new A();
            assertTrue(vh.compareAndSet(a, 0.0f, 1.0f));
            assertTrue(vh.compareAndSet(a, 1.0f, 0.0f));
            assertFalse(vh.compareAndSet(a, -0.0f, Float.NaN));
            assertTrue(vh.compareAndSet(a, 0.0f, Float.NaN));
            assertTrue(vh.compareAndSet(a, Float.NaN, 0.0f));
        }

        public static void main(String[] args) {
            new FieldFloatTest().run();
        }
    }

    public static class FieldDoubleTest extends VarHandleUnitTest {
        private static final VarHandle vh;

        public static class A {
            public double field;
        }

        static {
            try {
                vh = MethodHandles.lookup().findVarHandle(A.class, "field", double.class);
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }

        @Override
        protected void doTest() {
            A a = new A();
            assertTrue(vh.compareAndSet(a, 0.0, 1.0));
            assertTrue(vh.compareAndSet(a, 1.0, 0.0));
            assertFalse(vh.compareAndSet(a, -0.0, Double.NaN));
            assertTrue(vh.compareAndSet(a, 0.0, Double.NaN));
            assertTrue(vh.compareAndSet(a, Double.NaN, 0.0));
        }

        public static void main(String[] args) {
            new FieldDoubleTest().run();
        }
    }

    public static void main(String[] args) {
        FieldFloatTest.main(args);
        FieldDoubleTest.main(args);
    }
}
