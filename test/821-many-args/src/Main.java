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

public class Main {

  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    ensureJitCompiled(Main.class, "staticMethod");
    int a = staticMethod(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    assertEquals(42, a);

    ensureJitCompiled(Main.class, "staticMethodNonRange");
    a = staticMethodNonRange(1, 2, 3, 4, 5);
    assertEquals(42, a);

    staticMain = new Main();
    ensureJitCompiled(Main.class, "instanceMethod");
    a = staticMain.instanceMethod(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    assertEquals(42, a);

    ensureJitCompiled(Main.class, "instanceMethodNonRange");
    a = staticMain.instanceMethodNonRange(1, 2, 3, 4);
    assertEquals(42, a);
  }

  public static int staticMethod(
      int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
    assertEquals(10, j);
    assertEquals(9, i);
    assertEquals(8, h);
    assertEquals(7, g);
    assertEquals(6, f);
    assertEquals(5, e);
    assertEquals(4, d);
    assertEquals(3, c);
    assertEquals(2, b);
    assertEquals(1, a);
    return 42;
  }

  public int instanceMethod(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
    assertEquals(10, j);
    assertEquals(9, i);
    assertEquals(8, h);
    assertEquals(7, g);
    assertEquals(6, f);
    assertEquals(5, e);
    assertEquals(4, d);
    assertEquals(3, c);
    assertEquals(2, b);
    assertEquals(1, a);
    assertEquals(staticMain, this);
    return 42;
  }

  public static int staticMethodNonRange(int a, int b, int c, int d, int e) {
    assertEquals(5, e);
    assertEquals(4, d);
    assertEquals(3, c);
    assertEquals(2, b);
    assertEquals(1, a);
    return 42;
  }

  public int instanceMethodNonRange(int a, int b, int c, int d) {
    assertEquals(4, d);
    assertEquals(3, c);
    assertEquals(2, b);
    assertEquals(1, a);
    assertEquals(staticMain, this);
    return 42;
  }

  static Main staticMain;

  public static void assertEquals(int expected, int actual) {
    if (expected != actual) {
      throw new Error("Expected " + expected + ", got " + actual);
    }
  }

  public static void assertEquals(Object expected, Object actual) {
    if (expected != actual) {
      throw new Error("Expected " + expected + ", got " + actual);
    }
  }

  public static native void ensureJitCompiled(Class<?> cls, String methodName);
}
