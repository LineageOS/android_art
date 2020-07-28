/*
 * Copyright 2020 The Android Open Source Project
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
import java.lang.reflect.Field;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;

class Main {
  static final String DEX_FILE = System.getenv("DEX_LOCATION") + "/596-app-images.jar";
  static final String SECONDARY_DEX_FILE =
    System.getenv("DEX_LOCATION") + "/596-app-images-ex.jar";
  static final String LIBRARY_SEARCH_PATH = System.getProperty("java.library.path");

  static class Inner {
    final public static int abc = 10;
  }

  static class Nested {

  }

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    testAppImageLoaded();
    testInitializedClasses();
    testInternedStrings();
    testReloadInternedString();
    testClassesOutsideAppImage();
    testLoadingSecondaryAppImage();
  }

  public static native boolean checkAppImageLoaded(String name);
  public static native boolean checkAppImageContains(Class<?> klass);
  public static native boolean checkInitialized(Class<?> klass);

  public static void testAppImageLoaded() throws Exception {
    assertTrue("App image is loaded", checkAppImageLoaded("596-app-images"));
    assertTrue("App image contains Inner", checkAppImageContains(Inner.class));
  }

  public static void testInitializedClasses() throws Exception {
    assertInitialized(Inner.class);
    assertInitialized(Nested.class);
    assertInitialized(StaticFields.class);
    assertInitialized(StaticFieldsInitSub.class);
    assertInitialized(StaticFieldsInit.class);
    assertInitialized(StaticInternString.class);
  }

  private static void assertInitialized(Class<?> klass) {
    assertTrue(klass.toString() + " is preinitialized", checkInitialized(klass));
  }

  public static void testInternedStrings() throws Exception {
    StringBuffer sb = new StringBuffer();
    sb.append("java.");
    sb.append("abc.");
    sb.append("Action");

    String tmp = sb.toString();
    String intern = tmp.intern();

    assertNotSame("Dynamically constructed string is not interned", tmp, intern);
    assertEquals("Static string on initialized class is matches runtime interned string", intern,
        StaticInternString.intent);
    assertEquals("Static string on initialized class is pre-interned", BootInternedString.boot,
        BootInternedString.boot.intern());

    // TODO: Does this next check really provide us anything?
    Field f = StaticInternString.class.getDeclaredField("intent");
    assertEquals("String literals are interned properly", intern, f.get(null));

    assertEquals("String literals are interned properly across classes",
        StaticInternString.getIntent(), StaticInternString2.getIntent());
  }

  public static void testReloadInternedString() throws Exception {
    // reload the class StaticInternString, check whether static strings interned properly
    PathClassLoader loader = new PathClassLoader(DEX_FILE, LIBRARY_SEARCH_PATH, null);
    Class<?> staticInternString = loader.loadClass("StaticInternString");
    assertTrue("Class in app image isn't loaded a second time after loading dex file again",
        checkAppImageContains(staticInternString));

    Method getIntent = staticInternString.getDeclaredMethod("getIntent");
    assertEquals("Interned strings are still interned after multiple dex loads",
        StaticInternString.getIntent(), getIntent.invoke(staticInternString));
  }

  public static void testClassesOutsideAppImage() {
    assertFalse("App image doesn't contain non-optimized class",
        checkAppImageContains(NonOptimizedClass.class));
    assertFalse("App image didn't pre-initialize non-optimized class",
        checkInitialized(NonOptimizedClass.class));
  }

  public static void testLoadingSecondaryAppImage() throws Exception {
    final ClassLoader parent = Main.class.getClassLoader();

    // Initial check that the image isn't already loaded so we don't get bogus results below
    assertFalse("Secondary app image isn't already loaded",
        checkAppImageLoaded("596-app-images-ex"));

    PathClassLoader pcl = new PathClassLoader(SECONDARY_DEX_FILE, parent);

    assertTrue("Ensure app image is loaded if it should be",
        checkAppImageLoaded("596-app-images-ex"));

    Class<?> secondaryCls = pcl.loadClass("Secondary");
    assertTrue("Ensure Secondary class is in the app image if the CLC is correct",
        checkAppImageContains(secondaryCls));
    assertTrue("Ensure Secondary class is preinitialized if the CLC is correct",
        checkInitialized(secondaryCls));

    secondaryCls.getDeclaredMethod("go").invoke(null);
  }

  private static void assertTrue(String message, boolean flag) {
    if (flag) {
      return;
    }
    throw new AssertionError(message);
  }

  private static void assertEquals(String message, Object a, Object b) {
    StringBuilder sb = new StringBuilder(message != null ? message  : "");
    if (sb.length() > 0) {
      sb.append(" ");
    }
    sb.append("expected:<").append(a).append("> but was:<").append(b).append(">");
    assertTrue(sb.toString(), (a == null && b == null) || (a != null && a.equals(b)));
  }

  private static void assertFalse(String message, boolean flag) {
    assertTrue(message, !flag);
  }

  private static void assertNotSame(String message, Object a, Object b) {
    StringBuilder sb = new StringBuilder(message != null ? message  : "");
    if (sb.length() > 0) {
      sb.append(" ");
    }
    sb.append("unexpected sameness, found:<").append(a).append("> and:<").append(b).append(">");
    assertTrue(sb.toString(), a != b);
  }
}

class StaticFields {
  public static int abc;
}

class StaticFieldsInitSub extends StaticFieldsInit {
  final public static int def = 10;
}

class StaticFieldsInit {
  final public static int abc = 10;
}

class StaticInternString {
  final public static String intent = "java.abc.Action";
  static public String getIntent() {
    return intent;
  }
}

class BootInternedString {
  final public static String boot = "double";
}

class StaticInternString2 {
  final public static String intent = "java.abc.Action";

  static String getIntent() {
    return intent;
  }
}

class NonOptimizedClass {}
