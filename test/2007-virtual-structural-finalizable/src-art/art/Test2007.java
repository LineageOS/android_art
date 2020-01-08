/*
 * Copyright (C) 2019 The Android Open Source Project
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

package art;

import dalvik.system.InMemoryDexClassLoader;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Base64;
import java.util.concurrent.CountDownLatch;
import java.util.function.Supplier;
import java.util.concurrent.atomic.*;
import java.lang.ref.*;

public class Test2007 {
  public static final CountDownLatch finish_latch = new CountDownLatch(1);
  public static volatile int counter = 0;
  public static Object theObject = null;
  public static class Transform {
    public Transform() { }
    protected void finalize() throws Throwable {
      System.out.println("Should never be called!");
      // Do nothing.
    }
  }

  /**
   * base64 encoded class/dex file for
   * public static class Transform {
   *   public String greeting;
   *
   *   public Transform() {
   *     greeting = "Hello";
   *   }
   *   protected void finalize() throws Throwable {
   *     System.out.println("Finalizing");
   *     counter++;
   *     finish_latch.countDown();
   *   }
   * }
   */
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
"ZGV4CjAzNQCC9DECxo2lTpw7FCCSqZArgZe8ab49ywRoBQAAcAAAAHhWNBIAAAAAAAAAAKQEAAAe" +
"AAAAcAAAAA0AAADoAAAAAgAAABwBAAAEAAAANAEAAAUAAABUAQAAAQAAAHwBAADMAwAAnAEAAAYC" +
"AAAOAgAAGgIAACECAAAkAgAAPgIAAE4CAAByAgAAkgIAAK4CAADFAgAA2QIAAO0CAAABAwAAGAMA" +
"AD8DAABOAwAAWQMAAFwDAABgAwAAbQMAAHgDAACBAwAAiwMAAJkDAACjAwAAqQMAAK4DAAC3AwAA" +
"vgMAAAMAAAAEAAAABQAAAAYAAAAHAAAACAAAAAkAAAAKAAAACwAAAAwAAAANAAAADgAAABEAAAAR" +
"AAAADAAAAAAAAAASAAAADAAAAAACAAABAAgAGAAAAAIAAAAVAAAAAgALABcAAAAJAAYAGgAAAAEA" +
"AAAAAAAAAQAAABYAAAAGAAEAGwAAAAcAAAAAAAAACwAAABQAAAABAAAAAQAAAAcAAAAAAAAADwAA" +
"AIwEAABkBAAAAAAAAAIAAQABAAAA8gEAAAgAAABwEAMAAQAaAAIAWxAAAA4AAwABAAIAAAD4AQAA" +
"EwAAAGIAAwAaAQEAbiACABAAYAABANgAAAFnAAEAYgACAG4QBAAAAA4ADAAOPEsAEAAOeGlaAAAB" +
"AAAACAAGPGluaXQ+AApGaW5hbGl6aW5nAAVIZWxsbwABSQAYTGFydC9UZXN0MjAwNyRUcmFuc2Zv" +
"cm07AA5MYXJ0L1Rlc3QyMDA3OwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xvc2luZ0NsYXNzOwAe" +
"TGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xhc3M7ABpMZGFsdmlrL2Fubm90YXRpb24vVGhyb3dz" +
"OwAVTGphdmEvaW8vUHJpbnRTdHJlYW07ABJMamF2YS9sYW5nL09iamVjdDsAEkxqYXZhL2xhbmcv" +
"U3RyaW5nOwASTGphdmEvbGFuZy9TeXN0ZW07ABVMamF2YS9sYW5nL1Rocm93YWJsZTsAJUxqYXZh" +
"L3V0aWwvY29uY3VycmVudC9Db3VudERvd25MYXRjaDsADVRlc3QyMDA3LmphdmEACVRyYW5zZm9y" +
"bQABVgACVkwAC2FjY2Vzc0ZsYWdzAAljb3VudERvd24AB2NvdW50ZXIACGZpbmFsaXplAAxmaW5p" +
"c2hfbGF0Y2gACGdyZWV0aW5nAARuYW1lAANvdXQAB3ByaW50bG4ABXZhbHVlAIwBfn5EOHsiY29t" +
"cGlsYXRpb24tbW9kZSI6ImRlYnVnIiwiaGFzLWNoZWNrc3VtcyI6ZmFsc2UsIm1pbi1hcGkiOjEs" +
"InNoYS0xIjoiMTI5ZWU5ZjY3NTZjMzlkZjU3ZmYwNzg1ZDI1NmIyMzc3MjY0MmI3YyIsInZlcnNp" +
"b24iOiIyLjAuMTAtZGV2In0AAgUBHBwBGAoCAwEcGAICBAITBAkZFxAAAQEBAAEAgYAEnAMBBLwD" +
"AAAAAAEAAABNBAAAAgAAAFUEAABbBAAAgAQAAAAAAAABAAAAAAAAAAEAAAB4BAAAEAAAAAAAAAAB" +
"AAAAAAAAAAEAAAAeAAAAcAAAAAIAAAANAAAA6AAAAAMAAAACAAAAHAEAAAQAAAAEAAAANAEAAAUA" +
"AAAFAAAAVAEAAAYAAAABAAAAfAEAAAEgAAACAAAAnAEAAAMgAAACAAAA8gEAAAEQAAABAAAAAAIA" +
"AAIgAAAeAAAABgIAAAQgAAADAAAATQQAAAAgAAABAAAAZAQAAAMQAAADAAAAdAQAAAYgAAABAAAA" +
"jAQAAAAQAAABAAAApAQAAA==");


  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static final class GcThread extends Thread {
    public volatile boolean finished = false;
    public void run() {
      while (!finished) {
        Runtime.getRuntime().gc();
        System.runFinalization();
      }
    }
  }

  public static void doTest() throws Exception {
    // Try GC forever
    GcThread gc_thr = new GcThread();
    gc_thr.start();
    // Make a transform
    mktransform();
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    theObject = null;
    finish_latch.await();
    System.out.println("counter: " + counter);
    // Make sure we don't have any remaining things to finalize, eg obsolete objects or something.
    Runtime.getRuntime().gc();
    System.runFinalization();
    gc_thr.finished = true;
    gc_thr.join();
  }

  // Make sure there is never a transform in the frame of doTest.
  public static void mktransform() throws Exception {
    theObject = new Transform();
  }
}
