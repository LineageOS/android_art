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

public class Main {
  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    // Compile it to ensure we're calling compiled code.
    ensureJitCompiled(Main.class, "myMethod");
    myMethod(1, 2, 3, 4);
  }

  public static void assertEquals(float expected, float actual) {
    if (expected != actual) {
      throw new Error("Expected " + expected + " got " + actual);
    }
  }

  public static void assertEquals(double expected, double actual) {
    if (expected != actual) {
      throw new Error("Expected " + expected + " got " + actual);
    }
  }

  public static void myMethod(float a, double b, float c, float d) {
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
    assertEquals(4, d);
  }

  public static native void ensureJitCompiled(Class<?> cls, String name);
}
