/*
 * Copyright (C) 2018 The Android Open Source Project
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

import java.io.File;
import java.lang.reflect.Method;
import java.util.Base64;

public class Main {
  public static void main(String[] args) throws ClassNotFoundException {
    System.loadLibrary(args[0]);

    // Run the initialization routine. This will enable hidden API checks in
    // the runtime, in case they are not enabled by default.
    init();

    // Load the '-ex' APK and attach it to the boot class path.
    appendToBootClassLoader(DEX_EXTRA, /* isCorePlatform */ false);

    // Find the test class in boot class loader and verify that its members are hidden.
    Class<?> klass = Class.forName("art.Test2038", true, BOOT_CLASS_LOADER);
    assertFieldIsHidden(klass, "before set-policy");
    assertMethodIsHidden(klass, "before set-policy");

    int old_policy = disablePolicy();

    // Verify that the class members are not hidden.
    assertFieldNotHidden(klass, "after disable-policy");
    assertMethodNotHidden(klass, "after disable-policy");

    setPolicy(old_policy);

    assertFieldIsHidden(klass, "after set-policy 2");
    assertMethodIsHidden(klass, "after set-policy 2");
  }

  private static void assertMethodNotHidden(Class<?> klass, String msg) {
    try {
      klass.getDeclaredMethod("foo");
    } catch (NoSuchMethodException ex) {
      // Unexpected. Should not have thrown NoSuchMethodException.
      throw new RuntimeException("Method should be accessible " + msg);
    }
  }

  private static void assertFieldNotHidden(Class<?> klass, String msg) {
    try {
      klass.getDeclaredField("bar");
    } catch (NoSuchFieldException ex) {
      // Unexpected. Should not have thrown NoSuchFieldException.
      throw new RuntimeException("Field should be accessible " + msg);
    }
  }
  private static void assertMethodIsHidden(Class<?> klass, String msg) {
    try {
      klass.getDeclaredMethod("foo");
      // Unexpected. Should have thrown NoSuchMethodException.
      throw new RuntimeException("Method should not be accessible " + msg);
    } catch (NoSuchMethodException ex) {
    }
  }

  private static void assertFieldIsHidden(Class<?> klass, String msg) {
    try {
      klass.getDeclaredField("bar");
      // Unexpected. Should have thrown NoSuchFieldException.
      throw new RuntimeException("Field should not be accessible " + msg);
    } catch (NoSuchFieldException ex) {
    }
  }

  private static final String DEX_EXTRA =
      new File(System.getenv("DEX_LOCATION"), "2038-hiddenapi-jvmti-ext-ex.jar").getAbsolutePath();

  private static ClassLoader BOOT_CLASS_LOADER = Object.class.getClassLoader();

  // Native functions. Note that these are implemented in 674-hiddenapi/hiddenapi.cc.
  private static native void appendToBootClassLoader(String dexPath, boolean isCorePlatform);
  private static native void init();

  // Native function implemented in hiddenapi_ext.cc
  private static native int setPolicy(int new_policy);
  private static native int disablePolicy();
}
