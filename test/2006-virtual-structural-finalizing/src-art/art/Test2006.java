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

public class Test2006 {
  public static final CountDownLatch start_latch = new CountDownLatch(1);
  public static final CountDownLatch redefine_latch = new CountDownLatch(1);
  public static final CountDownLatch finish_latch = new CountDownLatch(1);
  public static volatile int start_counter = 0;
  public static volatile int finish_counter = 0;
  public static class Transform {
    public Transform() { }
    protected void finalize() throws Throwable {
      System.out.println("Finalizing");
      start_counter++;
      start_latch.countDown();
      redefine_latch.await();
      finish_counter++;
      finish_latch.countDown();
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
   *   protected void finalize() {
   *     System.out.println("NOTHING HERE!");
   *   }
   * }
   */
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
"ZGV4CjAzNQDtxu0Tsy2rLn9iTZHx3r+yuY0IuN+y1el4BAAAcAAAAHhWNBIAAAAAAAAAALQDAAAX" +
"AAAAcAAAAAkAAADMAAAAAgAAAPAAAAACAAAACAEAAAQAAAAYAQAAAQAAADgBAAAgAwAAWAEAAKoB" +
"AACyAQAAuQEAANMBAADjAQAABwIAACcCAAA+AgAAUgIAAGYCAAB6AgAAiQIAAJgCAACjAgAApgIA" +
"AKoCAAC3AgAAwQIAAMsCAADRAgAA1gIAAN8CAADmAgAAAgAAAAMAAAAEAAAABQAAAAYAAAAHAAAA" +
"CAAAAAkAAAANAAAADQAAAAgAAAAAAAAADgAAAAgAAACkAQAAAAAGABEAAAAHAAQAEwAAAAAAAAAA" +
"AAAAAAAAABAAAAAEAAEAFAAAAAUAAAAAAAAAAAAAAAEAAAAFAAAAAAAAAAsAAACkAwAAhAMAAAAA" +
"AAACAAEAAQAAAJgBAAAIAAAAcBADAAEAGgABAFsQAAAOAAMAAQACAAAAngEAAAgAAABiAAEAGgEK" +
"AG4gAgAQAA4ABgAOPEsACgAOeAAAAQAAAAYABjxpbml0PgAFSGVsbG8AGExhcnQvVGVzdDIwMDYk" +
"VHJhbnNmb3JtOwAOTGFydC9UZXN0MjAwNjsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdD" +
"bGFzczsAHkxkYWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwAVTGphdmEvaW8vUHJpbnRTdHJl" +
"YW07ABJMamF2YS9sYW5nL09iamVjdDsAEkxqYXZhL2xhbmcvU3RyaW5nOwASTGphdmEvbGFuZy9T" +
"eXN0ZW07AA1OT1RISU5HIEhFUkUhAA1UZXN0MjAwNi5qYXZhAAlUcmFuc2Zvcm0AAVYAAlZMAAth" +
"Y2Nlc3NGbGFncwAIZmluYWxpemUACGdyZWV0aW5nAARuYW1lAANvdXQAB3ByaW50bG4ABXZhbHVl" +
"AIwBfn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwiaGFzLWNoZWNrc3VtcyI6ZmFsc2Us" +
"Im1pbi1hcGkiOjEsInNoYS0xIjoiMTI5ZWU5ZjY3NTZjMzlkZjU3ZmYwNzg1ZDI1NmIyMzc3MjY0" +
"MmI3YyIsInZlcnNpb24iOiIyLjAuMTAtZGV2In0AAgIBFRgBAgMCDwQJEhcMAAEBAQABAIGABNgC" +
"AQT4AgAAAAACAAAAdQMAAHsDAACYAwAAAAAAAAAAAAAAAAAAEAAAAAAAAAABAAAAAAAAAAEAAAAX" +
"AAAAcAAAAAIAAAAJAAAAzAAAAAMAAAACAAAA8AAAAAQAAAACAAAACAEAAAUAAAAEAAAAGAEAAAYA" +
"AAABAAAAOAEAAAEgAAACAAAAWAEAAAMgAAACAAAAmAEAAAEQAAABAAAApAEAAAIgAAAXAAAAqgEA" +
"AAQgAAACAAAAdQMAAAAgAAABAAAAhAMAAAMQAAACAAAAlAMAAAYgAAABAAAApAMAAAAQAAABAAAA" +
"tAMAAA==");

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
    GcThread gc_thr = new GcThread();
    gc_thr.start();
    mktransform();
    start_latch.await();
    System.out.println("start_counter: " + start_counter);
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    redefine_latch.countDown();
    finish_latch.await();
    System.out.println("Finish_counter: " + finish_counter);
    gc_thr.finished = true;
    gc_thr.join();
  }
  public static void mktransform() throws Exception {
    Transform tr = new Transform();
  }
}
