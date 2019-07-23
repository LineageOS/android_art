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

public class SimpleLoop implements Runnable {
    static final int numberOfThreads = 2;
    volatile static boolean sExitFlag = false;
    volatile static boolean sEntered = false;
    int threadIndex;

    SimpleLoop(int index) {
        threadIndex = index;
    }

    public static void main() throws Exception {
        final Thread[] threads = new Thread[numberOfThreads];
        for (int t = 0; t < threads.length; t++) {
            threads[t] = new Thread(new SimpleLoop(t));
            threads[t].start();
        }
        for (Thread t : threads) {
            t.join();
        }

        System.out.println("Simple loop finishing");
    }

    public void $noinline$busyLoop() {
        Main.assertIsManaged();
        sEntered = true;
        for (;;) {
            if (sExitFlag) {
                break;
            }
        }
        Main.assertIsInterpreted();
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
