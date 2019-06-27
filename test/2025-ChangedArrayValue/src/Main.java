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

public class Main {

    public static void assertIntEquals(int expected, int result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }

    public class A {
        public int[] value = new int[3];
    }

    public int testLoop() {
        A x;
        x = new A();

        int []a0 = {0x7, 0x77, 0x707};
        int []b0 = {0x7007, 0x70007, 0x700007};

        for (int i = 0; i < 10; i++) {
            b0[0]++;

            if (i % 2 == 0) {
                a0 = b0;
            }

            x.value = a0;
        }

        assertIntEquals(x.value[0], 28689);
        return x.value[0];

    }

    public static void main(String[] args) {
        Main obj = new Main();
        obj.testLoop();
    }
}
