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

  /// CHECK-START: java.lang.String Main.inlinePolymorphic(java.lang.Object) inliner (before)
  /// CHECK:       InvokeVirtual method_name:java.lang.Object.toString

  /// CHECK-START: java.lang.String Main.inlinePolymorphic(java.lang.Object) inliner (after)
  /// CHECK-DAG:   InvokeVirtual method_name:java.lang.Object.toString
  /// CHECK-DAG:   InvokeVirtual method_name:java.lang.StringBuilder.toString intrinsic:StringBuilderToString
  public static String inlinePolymorphic(Object obj) {
    return obj.toString();
  }

  public static void assertEquals(String actual, String expected) {
    if (!expected.equals(actual)) {
      throw new Error("Expected " + expected + ", got " + actual);
    }
  }

  public static void main(String[] args) {
    assertEquals(inlinePolymorphic(new StringBuilder("abc")), "abc");
    assertEquals(inlinePolymorphic("def"), "def");
  }
}
