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

import dalvik.system.PathClassLoader;
import dalvik.system.VMRuntime;

import java.lang.ref.WeakReference;
import java.lang.reflect.Constructor;
import java.util.concurrent.atomic.AtomicBoolean;

public class Main {
    static final String DEX_FILE = System.getenv("DEX_LOCATION") + "/1002-notify-startup.jar";
    static final String LIBRARY_SEARCH_PATH = System.getProperty("java.library.path");
    static AtomicBoolean completed = new AtomicBoolean(false);

    public static void main(String[] args) {
        System.loadLibrary(args[0]);
        System.out.println("Startup completed: " + hasStartupCompleted());
        Thread workerThread = new WorkerThread();
        workerThread.start();
        do {
            resetStartupCompleted();
            VMRuntime.getRuntime().notifyStartupCompleted();
            Thread.yield();
        } while (!completed.get());
        try {
            workerThread.join();
        } catch (Throwable e) {
            System.err.println(e);
        }
        System.out.println("Startup completed: " + hasStartupCompleted());
    }

    private static class WorkerThread extends Thread {
        static final int NUM_ITERATIONS = 100;

        private WeakReference<Class<?>> $noinline$loadClassInLoader() throws Exception {
            ClassLoader loader = new PathClassLoader(
                    DEX_FILE, LIBRARY_SEARCH_PATH, ClassLoader.getSystemClassLoader());
            Class ret = loader.loadClass("Main");
            return new WeakReference(ret);
        }

        public void run() {
            for (int i = 0; i < NUM_ITERATIONS; ++i) {
                try {
                    WeakReference<Class<?>> ref = $noinline$loadClassInLoader();
                    Runtime.getRuntime().gc();
                    Thread.yield();
                    // Don't validate the unloading since app images will keep classes live (for now).
                } catch (Throwable e) {
                    System.err.println(e);
                    break;
                }
            }
            completed.set(true);
        }
    }

    private static native boolean hasStartupCompleted();
    private static native void resetStartupCompleted();
}
