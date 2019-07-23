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

// This test checks that FP registers spill offset is correctly recorded in the SlowPath; by causing
// asynchronous deoptimization in debuggable mode we observe the FP values in the interpreter.
public class FloatLoop implements Runnable {
    static final int numberOfThreads = 2;
    volatile static boolean sExitFlag = false;
    volatile static boolean sEntered = false;
    int threadIndex;

    FloatLoop(int index) {
        threadIndex = index;
    }

    public static void main() throws Exception {
        final Thread[] threads = new Thread[numberOfThreads];
        for (int t = 0; t < threads.length; t++) {
            threads[t] = new Thread(new FloatLoop(t));
            threads[t].start();
        }
        for (Thread t : threads) {
            t.join();
        }

        System.out.println("Float loop finishing");
    }

    static final float kFloatConst0 = 256.0f;
    static final float kFloatConst1 = 128.0f;
    static final int kArraySize = 128;
    volatile static float floatField;

    public void expectEqualToEither(float value, float expected0, float expected1) {
        if (value != expected0 && value != expected1) {
            throw new Error("Expected:  " + expected0 + " or "+ expected1 +
                            ", found: " + value);
        }
    }

    public void $noinline$busyLoop() {
        Main.assertIsManaged();

        // On Arm64:
        // This loop is likely to be vectorized which causes the full 16-byte Q-register to be saved
        // across slow paths.
        int[] array = new int[kArraySize];
        for (int i = 0; i < kArraySize; i++) {
            array[i]++;
        }

        sEntered = true;
        float s0 = kFloatConst0;
        float s1 = kFloatConst1;
        for (int i = 0; !sExitFlag; i++) {
            if (i % 2 == 0) {
                s0 += 2.0;
                s1 += 2.0;
            } else {
                s0 -= 2.0;
                s1 -= 2.0;
            }
            // SuspendCheckSlowPath must record correct stack offset for spilled FP registers.
        }
        Main.assertIsInterpreted();

        expectEqualToEither(s0, kFloatConst0, kFloatConst0 + 2.0f);
        expectEqualToEither(s1, kFloatConst1, kFloatConst1 + 2.0f);

        floatField = s0 + s1;
    }

    public void run() {
        if (threadIndex == 0) {
            while (!sEntered) {
              Thread.yield();
            }
            Main.deoptimizeAll();
            sExitFlag = true;
        } else {
            $noinline$busyLoop();
        }
    }
}
