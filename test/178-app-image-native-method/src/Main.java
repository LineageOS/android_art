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
    new CriticalSignatures();
    makeVisiblyInitialized();  // Make sure they are visibly initialized.

    $noinline$opt$test();
    $noinline$opt$testFast();
    $noinline$opt$testCritical();
    $noinline$opt$testMissing();
    $noinline$opt$testMissingFast();
    $noinline$opt$testMissingCritical();
    $noinline$opt$testCriticalSignatures();

    // For calls from AOT-compiled code, the first call shall use the resolution method
    // retrieved from the .bss and the second call should find the .bss entry populated
    // with the target method. Re-run these tests to exercise the second path.
    $noinline$opt$test();
    $noinline$opt$testFast();
    $noinline$opt$testCritical();
    $noinline$opt$testMissing();
    $noinline$opt$testMissingFast();
    $noinline$opt$testMissingCritical();
    $noinline$opt$testCriticalSignatures();

    new CriticalClinitCheck();
    sTestCriticalClinitCheckOtherThread.join();
  }

  static void $noinline$opt$test() {
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

  static void $noinline$opt$testFast() {
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

  static void $noinline$opt$testCritical() {
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

  static void $noinline$opt$testMissing() {
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

  static void $noinline$opt$testMissingFast() {
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

  static void $noinline$opt$testMissingCritical() {
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

  static void $noinline$opt$testCriticalSignatures() {
    System.out.println("testCriticalSignatures");
    long l = 0xf00000000L;
    assertEquals(42, CriticalSignatures.nativeILFFFFD(1, l + 2L, 3.0f, 4.0f, 5.0f, 6.0f, 7.0));
    assertEquals(42, CriticalSignatures.nativeLIFFFFD(l + 7L, 6, 5.0f, 4.0f, 3.0f, 2.0f, 1.0));
    assertEquals(42, CriticalSignatures.nativeFLIFFFD(1.0f, l + 2L, 3, 4.0f, 5.0f, 6.0f, 7.0));
    assertEquals(42, CriticalSignatures.nativeDDIIIIII(8.0, 7.0, 6, 5, 4, 3, 2, 1));
    assertEquals(42, CriticalSignatures.nativeDFFILIII(1.0, 2.0f, 3.0f, 4, l + 5L, 6, 7, 8));
    assertEquals(42, CriticalSignatures.nativeDDFILIII(8.0, 7.0, 6.0f, 5, l + 4L, 3, 2, 1));
    assertEquals(42, CriticalSignatures.nativeDDIFII(1.0, 2.0, 3, 4.0f, 5, 6));
    assertEquals(42, CriticalSignatures.nativeFullArgs(
        // Generated by script (then modified to close argument list):
        //   for i in {0..84}; \
        //     do echo "        0xf00000000L + $((i*3))L,"; \
        //     echo "        $((i*3+2)),"; \
        //  done
        0xf00000000L + 0L,
        2,
        0xf00000000L + 3L,
        5,
        0xf00000000L + 6L,
        8,
        0xf00000000L + 9L,
        11,
        0xf00000000L + 12L,
        14,
        0xf00000000L + 15L,
        17,
        0xf00000000L + 18L,
        20,
        0xf00000000L + 21L,
        23,
        0xf00000000L + 24L,
        26,
        0xf00000000L + 27L,
        29,
        0xf00000000L + 30L,
        32,
        0xf00000000L + 33L,
        35,
        0xf00000000L + 36L,
        38,
        0xf00000000L + 39L,
        41,
        0xf00000000L + 42L,
        44,
        0xf00000000L + 45L,
        47,
        0xf00000000L + 48L,
        50,
        0xf00000000L + 51L,
        53,
        0xf00000000L + 54L,
        56,
        0xf00000000L + 57L,
        59,
        0xf00000000L + 60L,
        62,
        0xf00000000L + 63L,
        65,
        0xf00000000L + 66L,
        68,
        0xf00000000L + 69L,
        71,
        0xf00000000L + 72L,
        74,
        0xf00000000L + 75L,
        77,
        0xf00000000L + 78L,
        80,
        0xf00000000L + 81L,
        83,
        0xf00000000L + 84L,
        86,
        0xf00000000L + 87L,
        89,
        0xf00000000L + 90L,
        92,
        0xf00000000L + 93L,
        95,
        0xf00000000L + 96L,
        98,
        0xf00000000L + 99L,
        101,
        0xf00000000L + 102L,
        104,
        0xf00000000L + 105L,
        107,
        0xf00000000L + 108L,
        110,
        0xf00000000L + 111L,
        113,
        0xf00000000L + 114L,
        116,
        0xf00000000L + 117L,
        119,
        0xf00000000L + 120L,
        122,
        0xf00000000L + 123L,
        125,
        0xf00000000L + 126L,
        128,
        0xf00000000L + 129L,
        131,
        0xf00000000L + 132L,
        134,
        0xf00000000L + 135L,
        137,
        0xf00000000L + 138L,
        140,
        0xf00000000L + 141L,
        143,
        0xf00000000L + 144L,
        146,
        0xf00000000L + 147L,
        149,
        0xf00000000L + 150L,
        152,
        0xf00000000L + 153L,
        155,
        0xf00000000L + 156L,
        158,
        0xf00000000L + 159L,
        161,
        0xf00000000L + 162L,
        164,
        0xf00000000L + 165L,
        167,
        0xf00000000L + 168L,
        170,
        0xf00000000L + 171L,
        173,
        0xf00000000L + 174L,
        176,
        0xf00000000L + 177L,
        179,
        0xf00000000L + 180L,
        182,
        0xf00000000L + 183L,
        185,
        0xf00000000L + 186L,
        188,
        0xf00000000L + 189L,
        191,
        0xf00000000L + 192L,
        194,
        0xf00000000L + 195L,
        197,
        0xf00000000L + 198L,
        200,
        0xf00000000L + 201L,
        203,
        0xf00000000L + 204L,
        206,
        0xf00000000L + 207L,
        209,
        0xf00000000L + 210L,
        212,
        0xf00000000L + 213L,
        215,
        0xf00000000L + 216L,
        218,
        0xf00000000L + 219L,
        221,
        0xf00000000L + 222L,
        224,
        0xf00000000L + 225L,
        227,
        0xf00000000L + 228L,
        230,
        0xf00000000L + 231L,
        233,
        0xf00000000L + 234L,
        236,
        0xf00000000L + 237L,
        239,
        0xf00000000L + 240L,
        242,
        0xf00000000L + 243L,
        245,
        0xf00000000L + 246L,
        248,
        0xf00000000L + 249L,
        251,
        0xf00000000L + 252L,
        254));
  }

  static void initializingCriticalClinitCheck() {
    // Called from CriticalClinitCheck.<clinit>().
    // Test @CriticalNative calls on the initializing thread.
    $noinline$opt$testCriticalClinitCheck();
    sTestCriticalClinitCheckOtherThread = new Thread() {
      public void run() {
        $noinline$opt$testCriticalClinitCheck();
      }
    };
    sTestCriticalClinitCheckOtherThread.start();
    // Sleep for a second to give the other thread an opportunity to run.
    // We're testing that it performs a clinit check and blocks until we
    // exit the class initializer after writing the output below.
    try {
      Thread.sleep(1000);
    } catch (InterruptedException ie) {
      throw new Error(ie);
    }
    System.out.println("initializingCriticalClinitCheck finished");
  }

  static void $noinline$opt$testCriticalClinitCheck() {
    assertEquals(42, CriticalClinitCheck.nativeMethodVoid());
    assertEquals(42, CriticalClinitCheck.nativeMethod(42));
    assertEquals(42, CriticalClinitCheck.nativeMethodWithManyParameters(
        11, 12L, 13.0f, 14.0d,
        21, 22L, 23.0f, 24.0d,
        31, 32L, 33.0f, 34.0d,
        41, 42L, 43.0f, 44.0d,
        51, 52L, 53.0f, 54.0d,
        61, 62L, 63.0f, 64.0d,
        71, 72L, 73.0f, 74.0d,
        81, 82L, 83.0f, 84.0d));
    System.out.println("testCriticalClinitCheck passed");
  }

  static Thread sTestCriticalClinitCheckOtherThread = null;

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

class CriticalSignatures {
  // The following signatures exercise ARM argument moving and serve
  // as an example of the optimizations performed by the assembler.
  // Moving arguments is a lot simpler for other architectures.

  // JNI compiler does not emit the CFG, so we cannot CHECK the "dissassembly (after)".

  // vstm sp, {d0-d2}         # f1, f2, f3, f4, d -- store floats as D regs together with double
  // mov r4, r0               # hidden arg
  // mov r0, r1               # i
  //                          # l stays in r2-r3
  @CriticalNative
  public static native int nativeILFFFFD(
      int i, long l, float f1, float f2, float f3, float f4, double d);

  // vstm sp, {s1-s3}         # f2, f3, f4 -- store floats up to alignment gap
  // vstr d2, [sp, #16]       # d
  // mov r4, r0               # hidden arg
  // mov r0, r2               # low(l)
  // mov r1, r3               # high(l)
  // ldr r2, [sp, #...]       # i
  // vmov r3, s0              # f1
  @CriticalNative
  public static native int nativeLIFFFFD(
      long l, int i, float f1, float f2, float f3, float f4, double d);

  // ldr  ip, [sp, #...]      # i
  // str  ip, [sp]            # i
  // add  ip, sp, #4          # Spilling multiple floats at an offset from SP
  // vstm ip, {s1-s5}         # f2, f3, f4, d
  // mov r4, r0               # hidden arg
  // vmov r0, s0              # f1
  //                          # l stays in r2-r3
  @CriticalNative
  public static native int nativeFLIFFFD(
      float f1, long l, int i, float f2, float f3, float f4, double d);

  // stm sp, {r1,r2,r3}       # i1, i2, i3 -- store ints together
  // ldrd r1, ip, [sp, #...]  # i4, i5
  // strd r1, ip, [sp, #12]   # i4, i5
  // ldr ip, [sp, #72]        # i6
  // str ip, [sp, #20]        # i6
  // mov r4, r0               # hidden arg
  // vmov r0, r1, d0          # d1
  // vmov r2, r3, d1          # d2
  @CriticalNative
  public static native int nativeDDIIIIII(
      double d1, double d2, int i1, int i2, int i3, int i4, int i5, int i6);

  // str r1, [sp]             # i1 -- cannot store with l due to alignment gap
  // strd r2, r3, [sp, #8]    # l
  // ldrd r1, ip, [sp, #...]  # i2, i3
  // strd r1, ip, [sp, #16]   # i2, i3
  // ldr ip, [sp, #...]       # i4
  // str ip, [sp, #24]        # i4
  // mov r4, r0               # hidden arg
  // vmov r0, r1, d0          # d
  // vmov r2, r3, d1          # f1, f2 -- move both floats together as double
  @CriticalNative
  public static native int nativeDFFILIII(
      double d, float f1, float f2, int i1, long l, int i2, int i3, int i4);

  // vstr s4, [sp]            # f
  // add ip, sp, #4           # Spilling multiple core registers at an offset from SP
  // stm ip, {r1,r2,r3}       # i1, l -- store int together with long
  // ldrd r1, ip, [sp, #...]  # i2, i3
  // strd r1, ip, [sp, #16]   # i2, i3
  // ldr ip, [sp, #...]       # i4
  // str ip, [sp, #24]        # i4
  // mov r4, r0               # hidden arg
  // vmov r0, r1, d0          # d1
  // vmov r2, r3, d1          # d2
  @CriticalNative
  public static native int nativeDDFILIII(
      double d1, double d2, float f, int i1, long l, int i2, int i3, int i4);

  // str r1, [sp]             # i1
  // vstr s4, [sp, #4]        # f
  // strd r2, r3, [sp, #8]    # i2, i3 -- store ints together with STRD
  // mov r4, r0               # hidden arg
  // vmov r0, r1, d0          # d1
  // vmov r2, r3, d1          # d2
  @CriticalNative
  public static native int nativeDDIFII(
      double d1, double d2, int i1, float f, int i2, int i3);

  // ...
  // ldr ip, [sp, #2112]      # int
  // str ip, [sp, #1000]      # int
  // add r1, sp, #2048        # Prepare to use LDRD for loading long from a large offset
  // ldrd r1, ip, [r1, #68]   # long
  // strd r1, ip, [sp, #1008] # long
  // ldr ip, [sp, #2124]      # int
  // str ip, [sp, #1016]      # int
  // ldr ip, [sp, #2128]      # low(long) -- copy the next long as two words because the offset
  // str ip, [sp, #1024]      # low(long) -- is too large for STRD and we only use 2 temps (r1, ip)
  // ldr ip, [sp, #2132]      # high(long)
  // str ip, [sp, #1028]      # high(long)
  // ...
  @CriticalNative
  public static native int nativeFullArgs(
      // Note: Numbered by dalvik registers, 0-254 (max 255 regs for invoke-*-range)
      //
      // Generated by script (then modified to close the argument list):
      //   for i in {0..84}; do echo "      long l$((i*3)),"; echo "      int i$(($i*3+2)),"; done
      long l0,
      int i2,
      long l3,
      int i5,
      long l6,
      int i8,
      long l9,
      int i11,
      long l12,
      int i14,
      long l15,
      int i17,
      long l18,
      int i20,
      long l21,
      int i23,
      long l24,
      int i26,
      long l27,
      int i29,
      long l30,
      int i32,
      long l33,
      int i35,
      long l36,
      int i38,
      long l39,
      int i41,
      long l42,
      int i44,
      long l45,
      int i47,
      long l48,
      int i50,
      long l51,
      int i53,
      long l54,
      int i56,
      long l57,
      int i59,
      long l60,
      int i62,
      long l63,
      int i65,
      long l66,
      int i68,
      long l69,
      int i71,
      long l72,
      int i74,
      long l75,
      int i77,
      long l78,
      int i80,
      long l81,
      int i83,
      long l84,
      int i86,
      long l87,
      int i89,
      long l90,
      int i92,
      long l93,
      int i95,
      long l96,
      int i98,
      long l99,
      int i101,
      long l102,
      int i104,
      long l105,
      int i107,
      long l108,
      int i110,
      long l111,
      int i113,
      long l114,
      int i116,
      long l117,
      int i119,
      long l120,
      int i122,
      long l123,
      int i125,
      long l126,
      int i128,
      long l129,
      int i131,
      long l132,
      int i134,
      long l135,
      int i137,
      long l138,
      int i140,
      long l141,
      int i143,
      long l144,
      int i146,
      long l147,
      int i149,
      long l150,
      int i152,
      long l153,
      int i155,
      long l156,
      int i158,
      long l159,
      int i161,
      long l162,
      int i164,
      long l165,
      int i167,
      long l168,
      int i170,
      long l171,
      int i173,
      long l174,
      int i176,
      long l177,
      int i179,
      long l180,
      int i182,
      long l183,
      int i185,
      long l186,
      int i188,
      long l189,
      int i191,
      long l192,
      int i194,
      long l195,
      int i197,
      long l198,
      int i200,
      long l201,
      int i203,
      long l204,
      int i206,
      long l207,
      int i209,
      long l210,
      int i212,
      long l213,
      int i215,
      long l216,
      int i218,
      long l219,
      int i221,
      long l222,
      int i224,
      long l225,
      int i227,
      long l228,
      int i230,
      long l231,
      int i233,
      long l234,
      int i236,
      long l237,
      int i239,
      long l240,
      int i242,
      long l243,
      int i245,
      long l246,
      int i248,
      long l249,
      int i251,
      long l252,
      int i254);
}

class CriticalClinitCheck {
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

  static {
    Main.initializingCriticalClinitCheck();
  }
}
