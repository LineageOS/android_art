/*
 * Copyright 2019 The Android Open Source Project
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

import dalvik.annotation.optimization.FastNative;
import dalvik.annotation.optimization.CriticalNative;

public class Main {

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    // To avoid going through resolution trampoline, make test classes visibly initialized.
    new Test();
    new TestFast();
    new TestCritical();
    new TestMissing();
    new TestMissingFast();
    new TestMissingCritical();
    makeVisiblyInitialized();  // Make sure they are visibly initialized.

    test();
    testFast();
    testCritical();
    testMissing();
    testMissingFast();
    testMissingCritical();
  }

  static void test() {
    System.out.println("test");
    assertEquals(42, Test.nativeMethodVoid());
    assertEquals(42, Test.nativeMethod(42));
    assertEquals(42, Test.nativeMethodWithManyParameters(
        11, 12L, 13.0f, 14.0d,
        21, 22L, 23.0f, 24.0d,
        31, 32L, 33.0f, 34.0d,
        41, 42L, 43.0f, 44.0d,
        51, 52L, 53.0f, 54.0d,
        61, 62L, 63.0f, 64.0d,
        71, 72L, 73.0f, 74.0d,
        81, 82L, 83.0f, 84.0d));
  }

  static void testFast() {
    System.out.println("testFast");
    assertEquals(42, TestFast.nativeMethodVoid());
    assertEquals(42, TestFast.nativeMethod(42));
    assertEquals(42, TestFast.nativeMethodWithManyParameters(
        11, 12L, 13.0f, 14.0d,
        21, 22L, 23.0f, 24.0d,
        31, 32L, 33.0f, 34.0d,
        41, 42L, 43.0f, 44.0d,
        51, 52L, 53.0f, 54.0d,
        61, 62L, 63.0f, 64.0d,
        71, 72L, 73.0f, 74.0d,
        81, 82L, 83.0f, 84.0d));
  }

  static void testCritical() {
    System.out.println("testCritical");
    assertEquals(42, TestCritical.nativeMethodVoid());
    assertEquals(42, TestCritical.nativeMethod(42));
    assertEquals(42, TestCritical.nativeMethodWithManyParameters(
        11, 12L, 13.0f, 14.0d,
        21, 22L, 23.0f, 24.0d,
        31, 32L, 33.0f, 34.0d,
        41, 42L, 43.0f, 44.0d,
        51, 52L, 53.0f, 54.0d,
        61, 62L, 63.0f, 64.0d,
        71, 72L, 73.0f, 74.0d,
        81, 82L, 83.0f, 84.0d));
  }

  static void testMissing() {
    System.out.println("testMissing");

    try {
      TestMissing.nativeMethodVoid();
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}

    try {
      TestMissing.nativeMethod(42);
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}

    try {
      TestMissing.nativeMethodWithManyParameters(
          11, 12L, 13.0f, 14.0d,
          21, 22L, 23.0f, 24.0d,
          31, 32L, 33.0f, 34.0d,
          41, 42L, 43.0f, 44.0d,
          51, 52L, 53.0f, 54.0d,
          61, 62L, 63.0f, 64.0d,
          71, 72L, 73.0f, 74.0d,
          81, 82L, 83.0f, 84.0d);
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}
  }

  static void testMissingFast() {
    System.out.println("testMissingFast");

    try {
      TestMissingFast.nativeMethodVoid();
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}

    try {
      TestMissingFast.nativeMethod(42);
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}

    try {
      TestMissingFast.nativeMethodWithManyParameters(
          11, 12L, 13.0f, 14.0d,
          21, 22L, 23.0f, 24.0d,
          31, 32L, 33.0f, 34.0d,
          41, 42L, 43.0f, 44.0d,
          51, 52L, 53.0f, 54.0d,
          61, 62L, 63.0f, 64.0d,
          71, 72L, 73.0f, 74.0d,
          81, 82L, 83.0f, 84.0d);
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}
  }

  static void testMissingCritical() {
    System.out.println("testMissingCritical");

    try {
      TestMissingCritical.nativeMethodVoid();
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}

    try {
      TestMissingCritical.nativeMethod(42);
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}

    try {
      TestMissingCritical.nativeMethodWithManyParameters(
          11, 12L, 13.0f, 14.0d,
          21, 22L, 23.0f, 24.0d,
          31, 32L, 33.0f, 34.0d,
          41, 42L, 43.0f, 44.0d,
          51, 52L, 53.0f, 54.0d,
          61, 62L, 63.0f, 64.0d,
          71, 72L, 73.0f, 74.0d,
          81, 82L, 83.0f, 84.0d);
      throw new Error("UNREACHABLE");
    } catch (LinkageError expected) {}
  }

  static void assertEquals(int expected, int actual) {
    if (expected != actual) {
      throw new AssertionError("Expected " + expected + " got " + actual);
    }
  }

  public static native void makeVisiblyInitialized();
}

class Test {
  public static native int nativeMethodVoid();

  public static native int nativeMethod(int i);

  public static native int nativeMethodWithManyParameters(
      int i1, long l1, float f1, double d1,
      int i2, long l2, float f2, double d2,
      int i3, long l3, float f3, double d3,
      int i4, long l4, float f4, double d4,
      int i5, long l5, float f5, double d5,
      int i6, long l6, float f6, double d6,
      int i7, long l7, float f7, double d7,
      int i8, long l8, float f8, double d8);
}

class TestFast {
  @FastNative
  public static native int nativeMethodVoid();

  @FastNative
  public static native int nativeMethod(int i);

  @FastNative
  public static native int nativeMethodWithManyParameters(
      int i1, long l1, float f1, double d1,
      int i2, long l2, float f2, double d2,
      int i3, long l3, float f3, double d3,
      int i4, long l4, float f4, double d4,
      int i5, long l5, float f5, double d5,
      int i6, long l6, float f6, double d6,
      int i7, long l7, float f7, double d7,
      int i8, long l8, float f8, double d8);
}

class TestCritical {
  @CriticalNative
  public static native int nativeMethodVoid();

  @CriticalNative
  public static native int nativeMethod(int i);

  @CriticalNative
  public static native int nativeMethodWithManyParameters(
      int i1, long l1, float f1, double d1,
      int i2, long l2, float f2, double d2,
      int i3, long l3, float f3, double d3,
      int i4, long l4, float f4, double d4,
      int i5, long l5, float f5, double d5,
      int i6, long l6, float f6, double d6,
      int i7, long l7, float f7, double d7,
      int i8, long l8, float f8, double d8);
}

class TestMissing {
  public static native int nativeMethodVoid();

  public static native int nativeMethod(int i);

  public static native int nativeMethodWithManyParameters(
      int i1, long l1, float f1, double d1,
      int i2, long l2, float f2, double d2,
      int i3, long l3, float f3, double d3,
      int i4, long l4, float f4, double d4,
      int i5, long l5, float f5, double d5,
      int i6, long l6, float f6, double d6,
      int i7, long l7, float f7, double d7,
      int i8, long l8, float f8, double d8);
}

class TestMissingFast {
  @FastNative
  public static native int nativeMethodVoid();

  @FastNative
  public static native int nativeMethod(int i);

  @FastNative
  public static native int nativeMethodWithManyParameters(
      int i1, long l1, float f1, double d1,
      int i2, long l2, float f2, double d2,
      int i3, long l3, float f3, double d3,
      int i4, long l4, float f4, double d4,
      int i5, long l5, float f5, double d5,
      int i6, long l6, float f6, double d6,
      int i7, long l7, float f7, double d7,
      int i8, long l8, float f8, double d8);
}

class TestMissingCritical {
  @CriticalNative
  public static native int nativeMethodVoid();

  @CriticalNative
  public static native int nativeMethod(int i);

  @CriticalNative
  public static native int nativeMethodWithManyParameters(
      int i1, long l1, float f1, double d1,
      int i2, long l2, float f2, double d2,
      int i3, long l3, float f3, double d3,
      int i4, long l4, float f4, double d4,
      int i5, long l5, float f5, double d5,
      int i6, long l6, float f6, double d6,
      int i7, long l7, float f7, double d7,
      int i8, long l8, float f8, double d8);
}
