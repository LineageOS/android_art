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
import java.io.File;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.util.Arrays;

public class Main {

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    // Enable hidden API checks in case they are disabled by default.
    init();

    // Put the classes with hiddenapi bits in the boot classpath.
    appendToBootClassLoader(DEX_PARENT_BOOT, /* isCorePlatform */ false);

    // Create a new class loader so the TestCase class sees the InheritAbstract classes in the boot
    // classpath.
    ClassLoader childLoader = new PathClassLoader(DEX_CHILD, Object.class.getClassLoader());
    Class<?> cls = Class.forName("TestCase", true, childLoader);
    Method m = cls.getDeclaredMethod("test");
    m.invoke(null);

    // Create a new native library which 'childLoader' can load.
    String absoluteLibraryPath = getNativeLibFileName(args[0]);

    // Do the test for JNI code.
    m = cls.getDeclaredMethod("testNative", String.class);
    m.invoke(null, createNativeLibCopy(absoluteLibraryPath));
  }

  // Tries to find the absolute path of the native library whose basename is 'arg'.
  private static String getNativeLibFileName(String arg) throws Exception {
    String libName = System.mapLibraryName(arg);
    Method libPathsMethod = Runtime.class.getDeclaredMethod("getLibPaths");
    libPathsMethod.setAccessible(true);
    String[] libPaths = (String[]) libPathsMethod.invoke(Runtime.getRuntime());
    String nativeLibFileName = null;
    for (String p : libPaths) {
      String candidate = p + libName;
      if (new File(candidate).exists()) {
        nativeLibFileName = candidate;
        break;
      }
    }
    if (nativeLibFileName == null) {
      throw new IllegalStateException("Didn't find " + libName + " in " +
          Arrays.toString(libPaths));
    }
    return nativeLibFileName;
  }

  // Copy native library to a new file with a unique name so it does not
  // conflict with other loaded instance of the same binary file.
  private static String createNativeLibCopy(String nativeLibFileName) throws Exception {
    String tempFileName = System.mapLibraryName("hiddenapitest");
    File tempFile = new File(System.getenv("DEX_LOCATION"), tempFileName);
    Files.copy(new File(nativeLibFileName).toPath(), tempFile.toPath());
    return tempFile.getAbsolutePath();
  }

  private static final String DEX_PARENT_BOOT =
      new File(new File(System.getenv("DEX_LOCATION"), "res"), "boot.jar").getAbsolutePath();
  private static final String DEX_CHILD =
      new File(System.getenv("DEX_LOCATION"), "817-hiddenapi-ex.jar").getAbsolutePath();

  private static native int appendToBootClassLoader(String dexPath, boolean isCorePlatform);
  private static native void init();
}
