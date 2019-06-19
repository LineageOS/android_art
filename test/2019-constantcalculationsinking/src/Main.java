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
/**
 *
 * L: Break operator doesn't break one basic block limitation for int,
 * for some reasons (most probably not a bug), there are two basic blocks for long type,
 * 1 sinking expected for int, 0 for long.
 * M: no limitations on basic blocks number, 1 constant calculation sinking expected for
 * each method
 *
 **/

public class Main {

    final int iterations = 1100;

    public static void assertIntEquals(int expected, int result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }

    public static void assertLongEquals(long expected, long result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }


    public int testLoopAddInt() {
        int testVar = 10000;
        int additionalVar = 10;

        outer:
            for (int i = 0; i < iterations; i++) {
                additionalVar += i;
                for (int k = 0; k < iterations; k++) {
                    additionalVar += k;
                    testVar += 5;
                    continue outer;
                }
            }
        assertIntEquals(testVar + additionalVar, 619960);
        return testVar + additionalVar;
    }

    public int testLoopSubInt() {
        int testVar = 10000;
        int additionalVar = 10;

        outer:
            for (int i = 0; i < iterations; i++) {
                additionalVar += i;
                for (int k = 0; k < iterations; k++) {
                    additionalVar += k;
                    testVar -= 5;
                    continue outer;
                }
            }
        assertIntEquals(testVar + additionalVar, 608960);
        return testVar + additionalVar;
    }

    public long testLoopSubLong() {
        long testVar = 10000;
        long additionalVar = 10;

        outer:
            for (long i = 0; i < iterations; i++) {
                additionalVar += i;
                for (long k = 0; k < iterations; k++) {
                    additionalVar += k;
                    testVar -= 5;
                    continue outer;
                }
            }
        assertLongEquals(testVar + additionalVar, 608960);
        return testVar + additionalVar;
    }

    public int testLoopMulInt(int n) {
        int testVar = 1;
        int additionalVar = 10;

        outer:
            for (int i = 0; i < 3; i++) {
                additionalVar += i + n * 2;
                for (int k = 0; k < 5; k++) {
                    additionalVar += k + n + k % 3 - i % 2 + n % 4 - i % 5
                                     + (k + 2) / 7 - (i - 5) / 3 + k * 3 - k / 2;
                    testVar *= 6;
                    continue outer;
                }
            }
        assertIntEquals(testVar + additionalVar, 324);
        return testVar + additionalVar;
    }

    public long testLoopMulLong(long n) {
        long testVar = 1;
        long additionalVar = 10;

        outer:
            for (long i = 0; i < 5; i++) {
                additionalVar += i + n;
                for (long k = 0; k < 5; k++) {
                    additionalVar += k + n + k % 3 - i % 2 + n % 4 - i % 5
                                     + (k + 2) / 7 - (i - 5) / 3 + k * 3 - k / 2;
                    testVar *= 6L;
                    continue outer;
                }
            }
        assertLongEquals(testVar + additionalVar, 7897);
        return testVar + additionalVar;
    }

    public int testLoopDivInt() {
        int testVar = 10000;
        int additionalVar = 10;

        outer:
            for (int i = 0; i < iterations; i++) {
                additionalVar += i;
                for (int k = 0; k < iterations; k++) {
                    additionalVar += k;
                    testVar /= 5;
                    continue outer;
                }
            }
        assertIntEquals(testVar + additionalVar, 604460);
        return testVar + additionalVar;
    }

    public long testLoopDivLong() {
        long testVar = 10000;
        long additionalVar = 10;

        outer:
            for (long i = 0; i < iterations; i++) {
                additionalVar += i;
                for (long k = 0; k < iterations; k++) {
                    additionalVar += k;
                    testVar /= 5;
                    continue outer;
                }
            }
        assertLongEquals(testVar + additionalVar, 604460);
        return testVar + additionalVar;
    }

    public int testLoopRemInt() {
        int testVar = 10000;
        int additionalVar = 10;

        outer:
            for (int i = 0; i < iterations; i++) {
                additionalVar += i;
                for (int k = 0; k < iterations; k++) {
                    additionalVar += k;
                    testVar %= 5;
                    continue outer;
                }
            }
        assertIntEquals(testVar + additionalVar, 604460);
        return testVar + additionalVar;
    }

    public long testLoopRemLong() {
        long testVar = 10000;
        long additionalVar = 10;

        outer:
            for (long i = 0; i < iterations; i++) {
                additionalVar += i;
                for (long k = 0; k < iterations; k++) {
                    additionalVar += k;
                    testVar %= 5;
                    continue outer;
                }
            }
        assertLongEquals(testVar + additionalVar, 604460);
        return testVar + additionalVar;
    }

    public long testLoopAddLong() {
        long testVar = 10000;
        long additionalVar = 10;

        outer:
            for (long i = 0; i < iterations; i++) {
                additionalVar += i;
                for (long k = 0; k < iterations; k++) {
                    additionalVar += k;
                    testVar += 5;
                    continue outer;
                }
            }
        assertLongEquals(testVar + additionalVar, 619960);
        return testVar + additionalVar;
    }

    public static void main(String[] args) {
        Main obj = new Main();
        obj.testLoopAddInt();
        obj.testLoopAddLong();
        obj.testLoopRemLong();
        obj.testLoopRemInt();
        obj.testLoopDivLong();
        obj.testLoopDivInt();
        obj.testLoopMulLong(10);
        obj.testLoopMulInt(10);
        obj.testLoopSubLong();
        obj.testLoopSubInt();
    }

}
