/*
 * Copyright (C) 2020 The Android Open Source Project
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

class Main {
  static final String TEST_NAME = "597-app-images-same-classloader";

  static final String DEX_FILE = System.getenv("DEX_LOCATION") + "/" + TEST_NAME + ".jar";
  static final String LIBRARY_SEARCH_PATH = System.getProperty("java.library.path");

  static final String SECONDARY_NAME = TEST_NAME + "-ex";
  static final String SECONDARY_DEX_FILE =
    System.getenv("DEX_LOCATION") + "/" + SECONDARY_NAME + ".jar";

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    testLoadingSecondaryAppImageInLoadedClassLoader();
  }

  public static native boolean checkAppImageLoaded(String name);
  public static native boolean checkAppImageContains(Class<?> klass);
  public static native boolean checkInitialized(Class<?> klass);

  public static void testLoadingSecondaryAppImageInLoadedClassLoader() throws Exception {
    // Initial check that the image isn't already loaded so we don't get bogus results below
    assertFalse("Secondary app image isn't already loaded",
        checkAppImageLoaded(SECONDARY_NAME));

    PathClassLoader pcl = new PathClassLoader(DEX_FILE, LIBRARY_SEARCH_PATH, null);
    pcl.addDexPath(SECONDARY_DEX_FILE);

    assertTrue("Ensure app image is loaded if it should be",
        checkAppImageLoaded(SECONDARY_NAME));

    Class<?> secondaryCls = pcl.loadClass("Secondary");
    assertTrue("Ensure Secondary class is in the app image",
        checkAppImageContains(secondaryCls));
    assertTrue("Ensure Secondary class is preinitialized", checkInitialized(secondaryCls));

    secondaryCls.getDeclaredMethod("go").invoke(null);
  }

  private static void assertTrue(String message, boolean flag) {
    if (flag) {
      return;
    }
    throw new AssertionError(message);
  }

  private static void assertFalse(String message, boolean flag) {
    assertTrue(message, !flag);
  }
}
