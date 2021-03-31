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

import dalvik.system.PathClassLoader;
import java.lang.reflect.Method;
import java.io.File;
import java.nio.ByteBuffer;
import java.util.Base64;

public class Main {
  private static void check(boolean expected, boolean actual, String message) {
    if (expected != actual) {
      System.err.println(
          "ERROR: " + message + " (expected=" + expected + ", actual=" + actual + ")");
      throw new Error("");
    }
  }

  private static ClassLoader singleLoader() {
    return new PathClassLoader(DEX_EXTRA, /*parent*/null);
  }

  private static void test(ClassLoader loader,
                           boolean expectedHasVdexFile,
                           boolean expectedBackedByOat,
                           boolean invokeMethod) throws Exception {
    // If ART created a vdex file, it must have verified all the classes.
    // That happens if and only if we expect a vdex at the end of the test but
    // do not expect it to have been loaded.
    boolean expectedClassesVerified = expectedHasVdexFile && !expectedBackedByOat;

    waitForVerifier();
    check(expectedClassesVerified, areClassesVerified(loader), "areClassesVerified");
    check(expectedHasVdexFile, hasVdexFile(loader), "hasVdexFile");
    check(expectedBackedByOat, isBackedByOatFile(loader), "isBackedByOatFile");
    check(expectedBackedByOat, areClassesPreverified(loader), "areClassesPreverified");

    if (invokeMethod) {
      loader.loadClass("art.ClassB").getDeclaredMethod("printHello").invoke(null);
    }

    if (expectedBackedByOat) {
      String filter = getCompilerFilter(loader.loadClass("art.ClassB"));
      if (!("verify".equals(filter))) {
        throw new Error("Expected verify, got " + filter);
      }
    }
  }

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    // Feature is disabled in debuggable mode because runtime threads are not
    // allowed to load classes.
    boolean featureEnabled = !isDebuggable();

    // SDK version not set. Background verification job should not have run
    // and vdex should not have been created.
    test(singleLoader(), /*hasVdex*/ false, /*backedByOat*/ false, /*invokeMethod*/ true);

    // Feature only enabled for target SDK version Q and later.
    setTargetSdkVersion(/* Q */ 29);

    // SDK version directory is now set. Background verification job should have run,
    // should have verified classes and written results to a vdex.
    test(singleLoader(), /*hasVdex*/ featureEnabled, /*backedByOat*/ false, /*invokeMethod*/ true);
    test(singleLoader(), /*hasVdex*/ featureEnabled, /*backedByOat*/ featureEnabled,
        /*invokeMethod*/ true);
  }

  private static native boolean isDebuggable();
  private static native int setTargetSdkVersion(int version);
  private static native void waitForVerifier();
  private static native boolean areClassesVerified(ClassLoader loader);
  private static native boolean hasVdexFile(ClassLoader loader);
  private static native boolean isBackedByOatFile(ClassLoader loader);
  private static native boolean areClassesPreverified(ClassLoader loader);
  private static native String getCompilerFilter(Class cls);

  private static final String DEX_LOCATION = System.getenv("DEX_LOCATION");
  private static final String DEX_EXTRA =
      new File(DEX_LOCATION, "692-vdex-secondary-loader-ex.jar").getAbsolutePath();
}
