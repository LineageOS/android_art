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

    public static void assertLongEquals(long expected, long result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }

    public byte loop1() {
        byte used1 = 1;
        byte used2 = 2;
        byte used3 = 3;
        byte used4 = 4;
        byte invar1 = 15;
        byte invar2 = 25;
        byte invar3 = 35;
        byte invar4 = 45;


        for (byte i = 0; i < 127; i++) {
            used1 -= (byte)(invar1 + invar2);
            used2 *= (byte)(invar2 - invar3);
            used3 += (byte)(invar3 * invar4);
            used4 /= (byte)(invar1 * invar2 - invar3 + invar4);
        }

        assertIntEquals((byte)(used1 + used2 + used3 + used4), -123);
        return (byte)(used1 + used2 + used3 + used4);
    }

    public long loop2() {
        double used1 = 1;
        double used2 = 2;
        double used3 = 3;
        double used4 = 4;
        double invar1 = 234234234234l;
        double invar2 = 2523423423423424l;
        double invar3 = 35234234234234234l;
        double invar4 = 45234234234234234l;

        for (double i = 0; i < 10000; i++) {
            used1 += invar1 + invar2;
            used2 *= invar2 - invar3;
            used3 -= invar3 * invar4;
            used4 /= invar1 * invar2 - invar3 + invar4;
        }
        assertLongEquals(Double.doubleToLongBits(used1 + used2 + used3 + used4),
            9218868437227405312l);
        return Double.doubleToLongBits(used1 + used2 + used3 + used4);

    }

    public static void main(String[] args) {
        byte res = new Main().loop1();
        long res1 = new Main().loop2();
    }
}
