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
    public class A {
        public int value;
    }

    public static void assertIntEquals(int expected, int result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }

    public long testLoop() {
        A x;
        x = new A();
        int a0 = 0x7;

        int i = 0;
        while (i < 10000000) {
            a0++;
            x.value = a0;

            if (i % 2 == 0) {
                i++;
                continue;
            }
            i = i + 2;
        }
        assertIntEquals(x.value, 5000008);
        return x.value;
    }

    public static void main(String[] args) {
        Main obj = new Main();
        obj.testLoop();
    }
}
