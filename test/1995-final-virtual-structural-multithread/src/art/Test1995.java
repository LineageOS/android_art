/*
 * Copyright (C) 2016 The Android Open Source Project
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

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Base64;
import java.util.concurrent.CountDownLatch;
public class Test1995 {
  private static final int NUM_THREADS = 20;
  // Don't perform more than this many repeats per thread to prevent OOMEs
  private static final int TASK_COUNT_LIMIT = 1000;

  public static final class Transform {
    public String greetingEnglish;
    public Transform() {
      this.greetingEnglish = "Hello";
    }
    public String sayHi() {
      return greetingEnglish + " from " + Thread.currentThread().getName();
    }
  }

  /**
   * base64 encoded class/dex file for
   * public static final class Transform {
   *   public String greetingEnglish;
   *   public String greetingFrench;
   *   public String greetingDanish;
   *   public String greetingJapanese;
   *
   *   public Transform() {
   *     this.greetingEnglish = "Hello World";
   *     this.greetingFrench = "Bonjour le Monde";
   *     this.greetingDanish = "Hej Verden";
   *     this.greetingJapanese = "こんにちは世界";
   *   }
   *   public String sayHi() {
   *     return sayHiEnglish() + ", " + sayHiFrench() + ", " + sayHiDanish() + ", " + sayHiJapanese() + " from " + Thread.currentThread().getName();
   *   }
   *   public String sayHiEnglish() {
   *     return greetingEnglish;
   *   }
   *   public String sayHiDanish() {
   *     return greetingDanish;
   *   }
   *   public String sayHiJapanese() {
   *     return greetingJapanese;
   *   }
   *   public String sayHiFrench() {
   *     return greetingFrench;
   *   }
   * }
   */
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
"ZGV4CjAzNQCsHrUqkb8cYgT2oYN7HlVbeOxJT/kONRvgBgAAcAAAAHhWNBIAAAAAAAAAABwGAAAl" +
"AAAAcAAAAAkAAAAEAQAABAAAACgBAAAEAAAAWAEAAAwAAAB4AQAAAQAAANgBAADoBAAA+AEAAEoD" +
"AABSAwAAVgMAAF4DAABwAwAAfAMAAIkDAACMAwAAkAMAAKoDAAC6AwAA3gMAAP4DAAASBAAAJgQA" +
"AEEEAABVBAAAZAQAAG8EAAByBAAAfwQAAIcEAACWBAAAnwQAAK8EAADABAAA0AQAAOIEAADoBAAA" +
"7wQAAPwEAAAKBQAAFwUAACYFAAAwBQAANwUAAK8FAAAIAAAACQAAAAoAAAALAAAADAAAAA0AAAAO" +
"AAAADwAAABIAAAAGAAAABQAAAAAAAAAHAAAABgAAAEQDAAAGAAAABwAAAAAAAAASAAAACAAAAAAA" +
"AAAAAAUAFwAAAAAABQAYAAAAAAAFABkAAAAAAAUAGgAAAAAAAwACAAAAAAAAABwAAAAAAAAAHQAA" +
"AAAAAAAeAAAAAAAAAB8AAAAAAAAAIAAAAAQAAwACAAAABgADAAIAAAAGAAEAFAAAAAYAAAAhAAAA" +
"BwACABUAAAAHAAAAFgAAAAAAAAARAAAABAAAAAAAAAAQAAAADAYAANUFAAAAAAAABwABAAIAAAAt" +
"AwAAQQAAAG4QAwAGAAwAbhAEAAYADAFuEAIABgAMAm4QBQAGAAwDcQAKAAAADARuEAsABAAMBCIF" +
"BgBwEAcABQBuIAgABQAaAAEAbiAIAAUAbiAIABUAbiAIAAUAbiAIACUAbiAIAAUAbiAIADUAGgAA" +
"AG4gCAAFAG4gCABFAG4QCQAFAAwAEQAAAAIAAQAAAAAAMQMAAAMAAABUEAAAEQAAAAIAAQAAAAAA" +
"NQMAAAMAAABUEAEAEQAAAAIAAQAAAAAAOQMAAAMAAABUEAIAEQAAAAIAAQAAAAAAPQMAAAMAAABU" +
"EAMAEQAAAAIAAQABAAAAJAMAABQAAABwEAYAAQAaAAUAWxABABoAAwBbEAIAGgAEAFsQAAAaACQA" +
"WxADAA4ACQAOPEtLS0sAEAAOABYADgATAA4AHAAOABkADgAAAAABAAAABQAGIGZyb20gAAIsIAAG" +
"PGluaXQ+ABBCb25qb3VyIGxlIE1vbmRlAApIZWogVmVyZGVuAAtIZWxsbyBXb3JsZAABTAACTEwA" +
"GExhcnQvVGVzdDE5OTUkVHJhbnNmb3JtOwAOTGFydC9UZXN0MTk5NTsAIkxkYWx2aWsvYW5ub3Rh" +
"dGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwASTGph" +
"dmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAGUxqYXZhL2xhbmcvU3RyaW5nQnVp" +
"bGRlcjsAEkxqYXZhL2xhbmcvVGhyZWFkOwANVGVzdDE5OTUuamF2YQAJVHJhbnNmb3JtAAFWAAth" +
"Y2Nlc3NGbGFncwAGYXBwZW5kAA1jdXJyZW50VGhyZWFkAAdnZXROYW1lAA5ncmVldGluZ0Rhbmlz" +
"aAAPZ3JlZXRpbmdFbmdsaXNoAA5ncmVldGluZ0ZyZW5jaAAQZ3JlZXRpbmdKYXBhbmVzZQAEbmFt" +
"ZQAFc2F5SGkAC3NheUhpRGFuaXNoAAxzYXlIaUVuZ2xpc2gAC3NheUhpRnJlbmNoAA1zYXlIaUph" +
"cGFuZXNlAAh0b1N0cmluZwAFdmFsdWUAdn5+RDh7ImNvbXBpbGF0aW9uLW1vZGUiOiJkZWJ1ZyIs" +
"Im1pbi1hcGkiOjEsInNoYS0xIjoiNjBkYTRkNjdiMzgxYzQyNDY3NzU3YzQ5ZmI2ZTU1NzU2ZDg4" +
"YTJmMyIsInZlcnNpb24iOiIxLjcuMTItZGV2In0AB+OBk+OCk+OBq+OBoeOBr+S4lueVjAACAgEi" +
"GAECAwITBBkbFxEABAEFAAEBAQEBAQEAgYAE7AUBAfgDAQGMBQEBpAUBAbwFAQHUBQAAAAAAAgAA" +
"AMYFAADMBQAAAAYAAAAAAAAAAAAAAAAAABAAAAAAAAAAAQAAAAAAAAABAAAAJQAAAHAAAAACAAAA" +
"CQAAAAQBAAADAAAABAAAACgBAAAEAAAABAAAAFgBAAAFAAAADAAAAHgBAAAGAAAAAQAAANgBAAAB" +
"IAAABgAAAPgBAAADIAAABgAAACQDAAABEAAAAQAAAEQDAAACIAAAJQAAAEoDAAAEIAAAAgAAAMYF" +
"AAAAIAAAAQAAANUFAAADEAAAAgAAAPwFAAAGIAAAAQAAAAwGAAAAEAAAAQAAABwGAAA=");


  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static final class MyThread extends Thread {
    public MyThread(CountDownLatch delay, int id) {
      super("Thread: " + id);
      this.thr_id = id;
      this.results = new ArrayList<>(TASK_COUNT_LIMIT);
      this.finish = false;
      this.delay = delay;
    }

    public void run() {
      delay.countDown();
      while (!finish && results.size() < TASK_COUNT_LIMIT) {
        Transform t = new Transform();
        results.add(t.sayHi());
      }
    }

    public void finish() throws Exception {
      finish = true;
      this.join();
    }

    public void Check() throws Exception {
      for (String s : results) {
        if (!s.equals("Hello from " + getName()) &&
            !s.equals("Hello, null, null, null from " + getName()) &&
            !s.equals("Hello World, Bonjour le Monde, Hej Verden, こんにちは世界 from " + getName())) {
          System.out.println("FAIL " + thr_id + ": Unexpected result: " + s);
        }
      }
    }

    public ArrayList<String> results;
    public volatile boolean finish;
    public int thr_id;
    public CountDownLatch delay;
  }

  public static MyThread[] startThreads(int num_threads) throws Exception {
    CountDownLatch cdl = new CountDownLatch(num_threads);
    MyThread[] res = new MyThread[num_threads];
    for (int i = 0; i < num_threads; i++) {
      res[i] = new MyThread(cdl, i);
      res[i].start();
    }
    cdl.await();
    return res;
  }
  public static void finishThreads(MyThread[] thrs) throws Exception {
    for (MyThread t : thrs) {
      t.finish();
    }
    for (MyThread t : thrs) {
      t.Check();
    }
  }

  public static void doTest() throws Exception {
    MyThread[] threads = startThreads(NUM_THREADS);
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    finishThreads(threads);
  }
}
