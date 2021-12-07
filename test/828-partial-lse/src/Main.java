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

class Main {

  public static void $noinline$testMain(Main m) {
    expectEquals(0, m.myField);
  }

  public static void main(String[] args) {
    $noinline$doTest(true);
    $noinline$doTest(false);
  }

  public static void $noinline$doTest(boolean testValue) {
    Main m = new Main();
    // LSE will find that this store can be removed, as both branches override the value with a new
    // one.
    m.myField = 42;
    if (testValue) {
      // LSE will remove this store as well, as it's the value after the store of 42 is removed.
      m.myField = 0;
      // This makes sure `m` gets materialized. At this point, the bug is that the partial LSE
      // optimization thinks the value incoming this block for `m.myField` is 42, however that
      // store, as well as the store to 0, have been removed.
      $noinline$testMain(m);
    } else {
      m.myField = 3;
      expectEquals(3, m.myField);
    }
  }

  public static void expectEquals(int expected, int actual) {
    if (expected != actual) {
      throw new Error("Expected " + expected + ", got " + actual);
    }
  }

  int myField = 0;
}
