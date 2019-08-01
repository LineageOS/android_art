/*
 * Copyright 2019 The Android Open Source Project
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

class Main {
  private static final boolean DEBUG = false;

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    makeVisiblyInitialized();
    Class<?> testClass = Class.forName("TestClass");  // Request initialized class.
    boolean is_visibly_initialized = isVisiblyInitialized(testClass);
    if (DEBUG) {
      System.out.println((is_visibly_initialized ? "Already" : "Not yet") + " visibly intialized");
    }
    if (!is_visibly_initialized) {
      synchronized(testClass) {
        Thread t = new Thread() {
          public void run() {
            // Regression test: This would have previously deadlocked
            // trying to lock on testClass. b/138561860
            makeVisiblyInitialized();
          }
        };
        t.start();
        t.join();
      }
      if (!isVisiblyInitialized(testClass)) {
        throw new Error("Should be visibly initialized now.");
      }
    }
  }

  public static native void makeVisiblyInitialized();
  public static native boolean isVisiblyInitialized(Class<?> klass);
}

class TestClass {
  static {
    // Add a static constructor that prevents initialization at compile time (app images).
    Main.isVisiblyInitialized(TestClass.class);  // Native call, discard result.
  }
}
