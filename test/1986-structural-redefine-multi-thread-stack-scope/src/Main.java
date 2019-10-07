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

import art.Redefinition;
import java.lang.invoke.*;
import java.lang.reflect.Field;
import java.util.Base64;
import java.util.concurrent.CountDownLatch;

public class Main {
  public static final class Transform {
    static {
    }

    public static Object foo = null;
  }

  /**
   * Base64 encoded dex bytes for:
   *
   * public static final class Transform {
   *   static {}
   *   public static Object bar = null;
   *   public static Object foo = null;
   * }
   */
  public static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQCjkRjcSr1RJO8FnnCjHV/8h6keJP/+P3WQAwAAcAAAAHhWNBIAAAAAAAAAANgCAAAQ"
                  + "AAAAcAAAAAYAAACwAAAAAQAAAMgAAAACAAAA1AAAAAMAAADkAAAAAQAAAPwAAAB0AgAAHAEAAFwB"
                  + "AABmAQAAbgEAAIABAACIAQAArAEAAMwBAADgAQAA6wEAAPYBAAD5AQAABgIAAAsCAAAQAgAAFgIA"
                  + "AB0CAAACAAAAAwAAAAQAAAAFAAAABgAAAAkAAAAJAAAABQAAAAAAAAAAAAQACwAAAAAABAAMAAAA"
                  + "AAAAAAAAAAAAAAAAAQAAAAQAAAABAAAAAAAAABEAAAAEAAAAAAAAAAcAAADIAgAApAIAAAAAAAAB"
                  + "AAAAAAAAAFABAAAGAAAAEgBpAAAAaQABAA4AAQABAAEAAABVAQAABAAAAHAQAgAAAA4ABwAOPAAF"
                  + "AA4AAAAACDxjbGluaXQ+AAY8aW5pdD4AEExNYWluJFRyYW5zZm9ybTsABkxNYWluOwAiTGRhbHZp"
                  + "ay9hbm5vdGF0aW9uL0VuY2xvc2luZ0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xh"
                  + "c3M7ABJMamF2YS9sYW5nL09iamVjdDsACU1haW4uamF2YQAJVHJhbnNmb3JtAAFWAAthY2Nlc3NG"
                  + "bGFncwADYmFyAANmb28ABG5hbWUABXZhbHVlAHZ+fkQ4eyJjb21waWxhdGlvbi1tb2RlIjoiZGVi"
                  + "dWciLCJtaW4tYXBpIjoxLCJzaGEtMSI6IjI4YmNlZjUwYWM4NTk3Y2YyMmU4OTJiMWJjM2EzYjky"
                  + "Yjc0ZTcwZTkiLCJ2ZXJzaW9uIjoiMS42LjMyLWRldiJ9AAICAQ4YAQIDAgoEGQ0XCAIAAgAACQEJ"
                  + "AIiABJwCAYGABLgCAAAAAAIAAACVAgAAmwIAALwCAAAAAAAAAAAAAAAAAAAPAAAAAAAAAAEAAAAA"
                  + "AAAAAQAAABAAAABwAAAAAgAAAAYAAACwAAAAAwAAAAEAAADIAAAABAAAAAIAAADUAAAABQAAAAMA"
                  + "AADkAAAABgAAAAEAAAD8AAAAASAAAAIAAAAcAQAAAyAAAAIAAABQAQAAAiAAABAAAABcAQAABCAA"
                  + "AAIAAACVAgAAACAAAAEAAACkAgAAAxAAAAIAAAC4AgAABiAAAAEAAADIAgAAABAAAAEAAADYAgAA");

  public static void assertEquals(Object a, Object b) {
    if (a != b) {
      throw new Error("Expected " + b + ", got " + a);
    }
  }

  public static void assertAllEquals(Object[] a, Object b) {
    boolean failed = false;
    String msg = "";
    for (int i = 0; i < a.length; i++) {
      if (a[i] != b) {
        failed = true;
        msg += "Expected " + b + ", got a[" + i + "] (" + a[i] + "), ";
      }
    }
    if (failed) {
      throw new Error(msg);
    }
  }

  public static void main(String[] args) throws Exception, Throwable {
    System.loadLibrary(args[0]);
    Field f = Transform.class.getDeclaredField("foo");
    Transform.foo = "THIS IS A FOO VALUE";
    assertEquals(f.get(null), Transform.foo);
    final int num_threads = 10;
    Object[] results = new Object[num_threads];
    Thread[] threads = new Thread[num_threads];
    CountDownLatch start_latch = new CountDownLatch(num_threads);
    CountDownLatch continue_latch = new CountDownLatch(1);
    for (int i = 0; i < num_threads; i++) {
      final int id = i;
      threads[id] =
          new Thread(
              () -> {
                try {
                  MethodHandle mh =
                      NativeFieldScopeCheck(
                          f,
                          () -> {
                            try {
                              start_latch.countDown();
                              continue_latch.await();
                            } catch (Exception e) {
                              throw new Error("failed!", e);
                            }
                          });
                  results[id] = mh.invokeExact();
                } catch (Throwable t) {
                  throw new Error("Failed", t);
                }
              },
              "Target thread " + id);
      threads[id].start();
    }
    start_latch.await();
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    continue_latch.countDown();
    for (Thread t : threads) {
      t.join();
    }
    assertAllEquals(results, Transform.foo);
  }

  // Hold the field as a ArtField, run the 'test' function, turn the ArtField into a MethodHandle
  // directly and return that.
  public static native MethodHandle NativeFieldScopeCheck(Field in, Runnable test);
}
