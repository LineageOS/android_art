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

import dalvik.system.InMemoryDexClassLoader;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Base64;
import java.util.concurrent.CountDownLatch;
import java.util.function.Supplier;

public class Test2001 {
  private static final int NUM_THREADS = 20;
  // Don't perform more than this many repeats per thread to prevent OOMEs
  private static final int TASK_COUNT_LIMIT = 1000;

  public static class Transform {
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
   * public static class Transform {
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
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQCnKPY06VRa4aM/zFW0MYLmRxT/NtXxD/H4BgAAcAAAAHhWNBIAAAAAAAAAADQGAAAl"
                  + "AAAAcAAAAAkAAAAEAQAABAAAACgBAAAEAAAAWAEAAAwAAAB4AQAAAQAAANgBAAAABQAA+AEAAEoD"
                  + "AABSAwAAVgMAAF4DAABwAwAAfAMAAIkDAACMAwAAkAMAAKoDAAC6AwAA3gMAAP4DAAASBAAAJgQA"
                  + "AEEEAABVBAAAZAQAAG8EAAByBAAAfwQAAIcEAACWBAAAnwQAAK8EAADABAAA0AQAAOIEAADoBAAA"
                  + "7wQAAPwEAAAKBQAAFwUAACYFAAAwBQAANwUAAMUFAAAIAAAACQAAAAoAAAALAAAADAAAAA0AAAAO"
                  + "AAAADwAAABIAAAAGAAAABQAAAAAAAAAHAAAABgAAAEQDAAAGAAAABwAAAAAAAAASAAAACAAAAAAA"
                  + "AAAAAAUAFwAAAAAABQAYAAAAAAAFABkAAAAAAAUAGgAAAAAAAwACAAAAAAAAABwAAAAAAAAAHQAA"
                  + "AAAAAAAeAAAAAAAAAB8AAAAAAAAAIAAAAAQAAwACAAAABgADAAIAAAAGAAEAFAAAAAYAAAAhAAAA"
                  + "BwACABUAAAAHAAAAFgAAAAAAAAABAAAABAAAAAAAAAAQAAAAJAYAAOsFAAAAAAAABwABAAIAAAAt"
                  + "AwAAQQAAAG4QAwAGAAwAbhAEAAYADAFuEAIABgAMAm4QBQAGAAwDcQAKAAAADARuEAsABAAMBCIF"
                  + "BgBwEAcABQBuIAgABQAaAAEAbiAIAAUAbiAIABUAbiAIAAUAbiAIACUAbiAIAAUAbiAIADUAGgAA"
                  + "AG4gCAAFAG4gCABFAG4QCQAFAAwAEQAAAAIAAQAAAAAAMQMAAAMAAABUEAAAEQAAAAIAAQAAAAAA"
                  + "NQMAAAMAAABUEAEAEQAAAAIAAQAAAAAAOQMAAAMAAABUEAIAEQAAAAIAAQAAAAAAPQMAAAMAAABU"
                  + "EAMAEQAAAAIAAQABAAAAJAMAABQAAABwEAYAAQAaAAUAWxABABoAAwBbEAIAGgAEAFsQAAAaACQA"
                  + "WxADAA4ACwAOPEtLS0sAEgAOABgADgAVAA4AHgAOABsADgAAAAABAAAABQAGIGZyb20gAAIsIAAG"
                  + "PGluaXQ+ABBCb25qb3VyIGxlIE1vbmRlAApIZWogVmVyZGVuAAtIZWxsbyBXb3JsZAABTAACTEwA"
                  + "GExhcnQvVGVzdDIwMDEkVHJhbnNmb3JtOwAOTGFydC9UZXN0MjAwMTsAIkxkYWx2aWsvYW5ub3Rh"
                  + "dGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwASTGph"
                  + "dmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAGUxqYXZhL2xhbmcvU3RyaW5nQnVp"
                  + "bGRlcjsAEkxqYXZhL2xhbmcvVGhyZWFkOwANVGVzdDIwMDEuamF2YQAJVHJhbnNmb3JtAAFWAAth"
                  + "Y2Nlc3NGbGFncwAGYXBwZW5kAA1jdXJyZW50VGhyZWFkAAdnZXROYW1lAA5ncmVldGluZ0Rhbmlz"
                  + "aAAPZ3JlZXRpbmdFbmdsaXNoAA5ncmVldGluZ0ZyZW5jaAAQZ3JlZXRpbmdKYXBhbmVzZQAEbmFt"
                  + "ZQAFc2F5SGkAC3NheUhpRGFuaXNoAAxzYXlIaUVuZ2xpc2gAC3NheUhpRnJlbmNoAA1zYXlIaUph"
                  + "cGFuZXNlAAh0b1N0cmluZwAFdmFsdWUAiwF+fkQ4eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWci"
                  + "LCJoYXMtY2hlY2tzdW1zIjpmYWxzZSwibWluLWFwaSI6MSwic2hhLTEiOiJmNjJiOGNlNmEwNTkw"
                  + "MDU0ZWYzNGExYWVkZTcwYjQ2NjY4ZThiNDlmIiwidmVyc2lvbiI6IjIuMC4xLWRldiJ9AAfjgZPj"
                  + "gpPjgavjgaHjga/kuJbnlYwAAgIBIhgBAgMCEwQJGxcRAAQBBQABAQEBAQEBAIGABOwFAQH4AwEB"
                  + "jAUBAaQFAQG8BQEB1AUAAAAAAAAAAgAAANwFAADiBQAAGAYAAAAAAAAAAAAAAAAAABAAAAAAAAAA"
                  + "AQAAAAAAAAABAAAAJQAAAHAAAAACAAAACQAAAAQBAAADAAAABAAAACgBAAAEAAAABAAAAFgBAAAF"
                  + "AAAADAAAAHgBAAAGAAAAAQAAANgBAAABIAAABgAAAPgBAAADIAAABgAAACQDAAABEAAAAQAAAEQD"
                  + "AAACIAAAJQAAAEoDAAAEIAAAAgAAANwFAAAAIAAAAQAAAOsFAAADEAAAAgAAABQGAAAGIAAAAQAA"
                  + "ACQGAAAAEAAAAQAAADQGAAA=");

  /*
   * base64 encoded class/dex file for
    package art;
    import java.util.function.Supplier;
    public class SubTransform extends art.Test2001.Transform implements Supplier<String> {
      public SubTransform() {
        super();
      }
      public String get() {
        return "from SUBCLASS: " + super.sayHi();
      }
    }
   */
  private static final byte[] SUB_DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQBawzkIDf9khFw00md41U4vIqRuhqBTjM+0BAAAcAAAAHhWNBIAAAAAAAAAAPwDAAAV"
                  + "AAAAcAAAAAgAAADEAAAABAAAAOQAAAAAAAAAAAAAAAgAAAAUAQAAAQAAAFQBAABAAwAAdAEAAAIC"
                  + "AAAKAgAADgIAABECAAAVAgAAKQIAAEMCAABiAgAAdgIAAIoCAAClAgAAxAIAAOMCAAD2AgAA+QIA"
                  + "AAEDAAASAwAAFwMAAB4DAAAoAwAALwMAAAQAAAAFAAAABgAAAAcAAAAIAAAACQAAAAoAAAANAAAA"
                  + "AgAAAAMAAAAAAAAAAgAAAAQAAAAAAAAAAwAAAAUAAAD8AQAADQAAAAcAAAAAAAAAAAADAAAAAAAA"
                  + "AAAAEAAAAAAAAQAQAAAAAQADAAAAAAABAAEAEQAAAAUAAwAAAAAABQACAA4AAAAFAAEAEgAAAAAA"
                  + "AAABAAAAAQAAAPQBAAAMAAAA7AMAAMsDAAAAAAAAAgABAAEAAADpAQAABQAAAG4QAgABAAwAEQAA"
                  + "AAQAAQACAAAA7QEAABYAAABvEAQAAwAMACIBBQBwEAUAAQAaAg8AbiAGACEAbiAGAAEAbhAHAAEA"
                  + "DAARAAEAAQABAAAA5AEAAAQAAABwEAMAAAAOAAYADjwABAAOAAkADgAAAAABAAAABgAAAAEAAAAE"
                  + "AAY8aW5pdD4AAj47AAFMAAJMTAASTGFydC9TdWJUcmFuc2Zvcm07ABhMYXJ0L1Rlc3QyMDAxJFRy"
                  + "YW5zZm9ybTsAHUxkYWx2aWsvYW5ub3RhdGlvbi9TaWduYXR1cmU7ABJMamF2YS9sYW5nL09iamVj"
                  + "dDsAEkxqYXZhL2xhbmcvU3RyaW5nOwAZTGphdmEvbGFuZy9TdHJpbmdCdWlsZGVyOwAdTGphdmEv"
                  + "dXRpbC9mdW5jdGlvbi9TdXBwbGllcjsAHUxqYXZhL3V0aWwvZnVuY3Rpb24vU3VwcGxpZXI8ABFT"
                  + "dWJUcmFuc2Zvcm0uamF2YQABVgAGYXBwZW5kAA9mcm9tIFNVQkNMQVNTOiAAA2dldAAFc2F5SGkA"
                  + "CHRvU3RyaW5nAAV2YWx1ZQCLAX5+RDh7ImNvbXBpbGF0aW9uLW1vZGUiOiJkZWJ1ZyIsImhhcy1j"
                  + "aGVja3N1bXMiOmZhbHNlLCJtaW4tYXBpIjoxLCJzaGEtMSI6ImY2MmI4Y2U2YTA1OTAwNTRlZjM0"
                  + "YTFhZWRlNzBiNDY2NjhlOGI0OWYiLCJ2ZXJzaW9uIjoiMi4wLjEtZGV2In0AAgIBExwEFwUXCxcI"
                  + "FwEAAAECAIGABMwDAcEg9AIBAZADAAAAAAAAAQAAAL0DAADkAwAAAAAAAAAAAAAAAAAADwAAAAAA"
                  + "AAABAAAAAAAAAAEAAAAVAAAAcAAAAAIAAAAIAAAAxAAAAAMAAAAEAAAA5AAAAAUAAAAIAAAAFAEA"
                  + "AAYAAAABAAAAVAEAAAEgAAADAAAAdAEAAAMgAAADAAAA5AEAAAEQAAACAAAA9AEAAAIgAAAVAAAA"
                  + "AgIAAAQgAAABAAAAvQMAAAAgAAABAAAAywMAAAMQAAACAAAA4AMAAAYgAAABAAAA7AMAAAAQAAAB"
                  + "AAAA/AMAAA==");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static Supplier<String> mkTransform() {
    try {
      return (Supplier<String>)
          (new InMemoryDexClassLoader(
                  ByteBuffer.wrap(SUB_DEX_BYTES), Test2001.class.getClassLoader())
              .loadClass("art.SubTransform")
              .newInstance());
    } catch (Exception e) {
      return () -> {
        return e.toString();
      };
    }
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
        Supplier<String> t = mkTransform();
        results.add(t.get());
      }
    }

    public void finish() throws Exception {
      finish = true;
      this.join();
    }

    public void Check() throws Exception {
      for (String s : results) {
        if (!s.equals("from SUBCLASS: Hello from " + getName())
            && !s.equals("from SUBCLASS: Hello, null, null, null from " + getName())
            && !s.equals(
                "from SUBCLASS: Hello World, Bonjour le Monde, Hej Verden, こんにちは世界 from "
                    + getName())) {
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
