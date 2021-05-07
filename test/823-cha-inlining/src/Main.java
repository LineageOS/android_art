/*
 * Copyright (C) 2021 The Android Open Source Project
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

interface Itf {
  // We make the methods below directly throw instead of using the $noinline$
  // directive to get the inliner actually try to inline but decide not to.
  // This will then make the compiler try to generate an HInvokeVirtual instead
  // of an HInvokeInterface.

  public default void m() throws Exception {
    throw new Exception("Don't inline me");
  }
  public default void mConflict() throws Exception {
    throw new Exception("Don't inline me");
  }
}

// This is redefined in src2 with a mConflict method.
interface Itf2 {
}

interface Itf3 extends Itf, Itf2 {
}

class Itf3Impl implements Itf3 {
}

interface Itf4 extends Itf, Itf2 {
  public default void m() throws Exception {
    throw new Exception("Don't inline me");
  }
}

class Itf4Impl implements Itf4 {
}


public class Main implements Itf, Itf2 {

  public static void main(String[] args) {
    System.loadLibrary(args[0]);

    // Execute enough time to populate inline caches.
    for (int i = 0; i < 100000; ++i) {
      try {
        $noinline$doCallDefault();
      } catch (Exception e) {
        // Expected.
      }
    }
    ensureJitCompiled(Main.class, "$noinline$doCallDefault");
    try {
      $noinline$doCallDefault();
      throw new Error("Expected exception");
    } catch (Exception e) {
      // Expected.
    }

    ensureJitCompiled(Main.class, "$noinline$doCallDefaultConflict");
    try {
      $noinline$doCallDefaultConflict();
      throw new Error("Expected IncompatibleClassChangeError");
    } catch (Exception e) {
      throw new Error("Unexpected exception");
    } catch (IncompatibleClassChangeError e) {
      // Expected.
    }

    // Execute enough time to populate inline caches.
    for (int i = 0; i < 100000; ++i) {
      try {
        $noinline$doCallDefaultConflictItf3();
      } catch (Throwable t) {
        // Expected.
      }
    }
    ensureJitCompiled(Main.class, "$noinline$doCallDefaultConflictItf3");
    try {
      $noinline$doCallDefaultConflictItf3();
      throw new Error("Expected IncompatibleClassChangeError");
    } catch (Exception e) {
      throw new Error("Unexpected exception " + e);
    } catch (IncompatibleClassChangeError e) {
      // Expected.
    }

    ensureJitCompiled(Main.class, "$noinline$doCallDefaultConflictItf4");
    try {
      $noinline$doCallDefaultConflictItf4();
      throw new Error("Expected IncompatibleClassChangeError");
    } catch (Exception e) {
      throw new Error("Unexpected exception");
    } catch (IncompatibleClassChangeError e) {
      // Expected.
    }
  }

  public static void $noinline$doCallDefault() throws Exception {
    itf.m();
  }

  public static void $noinline$doCallDefaultConflict() throws Exception {
    itf.mConflict();
  }

  public static void $noinline$doCallDefaultConflictItf3() throws Exception {
    itf3.mConflict();
  }

  public static void $noinline$doCallDefaultConflictItf4() throws Exception {
    itf4.mConflict();
  }

  static Itf itf = new Main();
  static Itf3 itf3 = new Itf3Impl();
  static Itf4 itf4 = new Itf4Impl();

  private static native void ensureJitCompiled(Class<?> cls, String methodName);
}
