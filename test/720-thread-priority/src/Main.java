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

import java.util.concurrent.CountDownLatch;

public class Main {
   static final CountDownLatch processStarted = new CountDownLatch(1);
   static final CountDownLatch prioritySet = new CountDownLatch(1);

   static int initialPlatformPriority = 0;
   static int maxPlatformPriority = 0;

   static class MyThread extends Thread {
        public void run() {
            try {
                int priority = getThreadPlatformPriority();
                if (priority != initialPlatformPriority) {
                    throw new Error("Expected " + initialPlatformPriority + ", got " + priority);
                }
                processStarted.countDown();
                prioritySet.await();
                priority = getThreadPlatformPriority();
                if (priority != maxPlatformPriority) {
                    throw new Error("Expected " + maxPlatformPriority + ", got " + priority);
                }
            } catch (Exception e) {
                throw new Error(e);
            }
        }
    }

    public static void main(String[] args) throws Exception {
        System.loadLibrary(args[0]);

        // Fetch priorities from the main thread to know what to compare against.
        int javaPriority = Thread.currentThread().getPriority();
        initialPlatformPriority = getThreadPlatformPriority();
        Thread.currentThread().setPriority(Thread.MAX_PRIORITY);
        maxPlatformPriority = getThreadPlatformPriority();
        Thread.currentThread().setPriority(javaPriority);

        MyThread t1 = new MyThread();
        t1.start();
        processStarted.await();

        t1.setPriority(Thread.MAX_PRIORITY);
        prioritySet.countDown();
        t1.join();
    }

    private static native int getThreadPlatformPriority();
}
