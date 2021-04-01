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

public class TestCase {

  public static void test() {
    // This call should be successful as the method is accessible through the interface.
    int value = new InheritAbstract().methodPublicSdkNotInAbstractParent();
    if (value != 42) {
      throw new Error("Expected 42, got " + value);
    }
  }

  public static void testNative(String library) {
    System.load(library);
    int value = testNativeInternal();
    if (value != 42) {
      throw new Error("Expected 42, got " + value);
    }
  }

  public static native int testNativeInternal();
}
