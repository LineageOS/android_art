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
    try {
      $noinline$doTest(args);
      throw new Error("Expected ArrayStoreException");
    } catch (ArrayStoreException e) {
      // expected
      check(e, mainLine, methodLine, "$noinline$doTest");
    }
  }

  public static void $noinline$doTest(String[] args) {
    Object[] o = new String[2];
    o[0] = args;
  }

  public static int mainLine = 21;
  public static int methodLine = 31;

  static void check(ArrayStoreException ase, int mainLine, int methodLine, String methodName) {
    StackTraceElement[] trace = ase.getStackTrace();
    checkElement(trace[0], "Main", methodName, "Main.java", methodLine);
    checkElement(trace[1], "Main", "main", "Main.java", mainLine);
  }

  static void checkElement(StackTraceElement element,
                           String declaringClass, String methodName,
                           String fileName, int lineNumber) {
    assertEquals(declaringClass, element.getClassName());
    assertEquals(methodName, element.getMethodName());
    assertEquals(fileName, element.getFileName());
    assertEquals(lineNumber, element.getLineNumber());
  }

  static void assertEquals(Object expected, Object actual) {
    if (!expected.equals(actual)) {
      String msg = "Expected \"" + expected + "\" but got \"" + actual + "\"";
      throw new AssertionError(msg);
    }
  }

  static void assertEquals(int expected, int actual) {
    if (expected != actual) {
      throw new AssertionError("Expected " + expected + " got " + actual);
    }
  }
}
