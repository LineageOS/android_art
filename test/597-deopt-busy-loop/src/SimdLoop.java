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

// This test checks that values (e.g. induction variable) are properly set for SuspendCheck
// environment in the SIMD loop; by causing asynchronous deoptimization in debuggable mode we
// we observe the values.
public class SimdLoop implements Runnable {
    static final int numberOfThreads = 2;
    volatile static boolean sExitFlag = false;
    volatile static boolean sEntered = false;
    int threadIndex;

    SimdLoop(int index) {
        threadIndex = index;
    }

    public static void main() throws Exception {
        final Thread[] threads = new Thread[numberOfThreads];
        for (int t = 0; t < threads.length; t++) {
            threads[t] = new Thread(new SimdLoop(t));
            threads[t].start();
        }
        for (Thread t : threads) {
            t.join();
        }

        System.out.println("Simd loop finishing");
    }

    static final int kArraySize = 3000000;

    public void expectEqual(int value, int expected) {
        if (value != expected) {
            throw new Error("Expected:  " + expected + ", found: " + value);
        }
    }

    public void $noinline$busyLoop() {
        Main.assertIsManaged();

        int[] array = new int[kArraySize];
        sEntered = true;

        // On Arm64:
        // These loops are likely to be vectorized; when deoptimizing to interpreter the induction
        // variable i will be set to wrong value (== 0).
        //
        // Copy-paste instead of nested loop is here to avoid extra loop suspend check.
        for (int i = 0; i < kArraySize; i++) {
            array[i]++;
        }
        for (int i = 0; i < kArraySize; i++) {
            array[i]++;
        }
        for (int i = 0; i < kArraySize; i++) {
            array[i]++;
        }
        for (int i = 0; i < kArraySize; i++) {
            array[i]++;
        }

        // We might have managed to execute the whole loop before deoptimizeAll() happend.
        if (sExitFlag) {
            Main.assertIsInterpreted();
        }
        // Regression: the value of the induction variable might have been set to 0 when
        // deoptimizing causing to have another array[0]++.
        expectEqual(array[0], 4);
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
