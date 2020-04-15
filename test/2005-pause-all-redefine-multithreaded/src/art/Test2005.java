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

import java.lang.reflect.Field;
import java.util.*;
import java.util.concurrent.CountDownLatch;
public class Test2005 {
  private static final int NUM_THREADS = 20;
  private static final String DEFAULT_VAL = "DEFAULT_VALUE";

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
   *     return sayHiEnglish() + ", " + sayHiFrench() + ", " + sayHiDanish() + ", " +
   * sayHiJapanese() + " from " + Thread.currentThread().getName();
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
      "ZGV4CjAzNQAgJ1QXHJ8PAODMKTV14wyH4oKGOMK1yyL4BgAAcAAAAHhWNBIAAAAAAAAAADQGAAAl"
      + "AAAAcAAAAAkAAAAEAQAABAAAACgBAAAEAAAAWAEAAAwAAAB4AQAAAQAAANgBAAAABQAA+AEAAEoD"
      + "AABSAwAAVgMAAF4DAABwAwAAfAMAAIkDAACMAwAAkAMAAKoDAAC6AwAA3gMAAP4DAAASBAAAJgQA"
      + "AEEEAABVBAAAZAQAAG8EAAByBAAAfwQAAIcEAACWBAAAnwQAAK8EAADABAAA0AQAAOIEAADoBAAA"
      + "7wQAAPwEAAAKBQAAFwUAACYFAAAwBQAANwUAAMUFAAAIAAAACQAAAAoAAAALAAAADAAAAA0AAAAO"
      + "AAAADwAAABIAAAAGAAAABQAAAAAAAAAHAAAABgAAAEQDAAAGAAAABwAAAAAAAAASAAAACAAAAAAA"
      + "AAAAAAUAFwAAAAAABQAYAAAAAAAFABkAAAAAAAUAGgAAAAAAAwACAAAAAAAAABwAAAAAAAAAHQAA"
      + "AAAAAAAeAAAAAAAAAB8AAAAAAAAAIAAAAAQAAwACAAAABgADAAIAAAAGAAEAFAAAAAYAAAAhAAAA"
      + "BwACABUAAAAHAAAAFgAAAAAAAAARAAAABAAAAAAAAAAQAAAAJAYAAOsFAAAAAAAABwABAAIAAAAt"
      + "AwAAQQAAAG4QAwAGAAwAbhAEAAYADAFuEAIABgAMAm4QBQAGAAwDcQAKAAAADARuEAsABAAMBCIF"
      + "BgBwEAcABQBuIAgABQAaAAEAbiAIAAUAbiAIABUAbiAIAAUAbiAIACUAbiAIAAUAbiAIADUAGgAA"
      + "AG4gCAAFAG4gCABFAG4QCQAFAAwAEQAAAAIAAQAAAAAAMQMAAAMAAABUEAAAEQAAAAIAAQAAAAAA"
      + "NQMAAAMAAABUEAEAEQAAAAIAAQAAAAAAOQMAAAMAAABUEAIAEQAAAAIAAQAAAAAAPQMAAAMAAABU"
      + "EAMAEQAAAAIAAQABAAAAJAMAABQAAABwEAYAAQAaAAUAWxABABoAAwBbEAIAGgAEAFsQAAAaACQA"
      + "WxADAA4ACQAOPEtLS0sAEAAOABYADgATAA4AHAAOABkADgAAAAABAAAABQAGIGZyb20gAAIsIAAG"
      + "PGluaXQ+ABBCb25qb3VyIGxlIE1vbmRlAApIZWogVmVyZGVuAAtIZWxsbyBXb3JsZAABTAACTEwA"
      + "GExhcnQvVGVzdDIwMDUkVHJhbnNmb3JtOwAOTGFydC9UZXN0MjAwNTsAIkxkYWx2aWsvYW5ub3Rh"
      + "dGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwASTGph"
      + "dmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAGUxqYXZhL2xhbmcvU3RyaW5nQnVp"
      + "bGRlcjsAEkxqYXZhL2xhbmcvVGhyZWFkOwANVGVzdDIwMDUuamF2YQAJVHJhbnNmb3JtAAFWAAth"
      + "Y2Nlc3NGbGFncwAGYXBwZW5kAA1jdXJyZW50VGhyZWFkAAdnZXROYW1lAA5ncmVldGluZ0Rhbmlz"
      + "aAAPZ3JlZXRpbmdFbmdsaXNoAA5ncmVldGluZ0ZyZW5jaAAQZ3JlZXRpbmdKYXBhbmVzZQAEbmFt"
      + "ZQAFc2F5SGkAC3NheUhpRGFuaXNoAAxzYXlIaUVuZ2xpc2gAC3NheUhpRnJlbmNoAA1zYXlIaUph"
      + "cGFuZXNlAAh0b1N0cmluZwAFdmFsdWUAiwF+fkQ4eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWci"
      + "LCJoYXMtY2hlY2tzdW1zIjpmYWxzZSwibWluLWFwaSI6MSwic2hhLTEiOiI5N2RmNmVkNzlhNzQw"
      + "ZWVhMzM4MmNiNWRhOTIyYmI1YmJjMDg2NDMzIiwidmVyc2lvbiI6IjIuMC45LWRldiJ9AAfjgZPj"
      + "gpPjgavjgaHjga/kuJbnlYwAAgIBIhgBAgMCEwQZGxcRAAQBBQABAQEBAQEBAIGABOwFAQH4AwEB"
      + "jAUBAaQFAQG8BQEB1AUAAAAAAAAAAgAAANwFAADiBQAAGAYAAAAAAAAAAAAAAAAAABAAAAAAAAAA"
      + "AQAAAAAAAAABAAAAJQAAAHAAAAACAAAACQAAAAQBAAADAAAABAAAACgBAAAEAAAABAAAAFgBAAAF"
      + "AAAADAAAAHgBAAAGAAAAAQAAANgBAAABIAAABgAAAPgBAAADIAAABgAAACQDAAABEAAAAQAAAEQD"
      + "AAACIAAAJQAAAEoDAAAEIAAAAgAAANwFAAAAIAAAAQAAAOsFAAADEAAAAgAAABQGAAAGIAAAAQAA"
      + "ACQGAAAAEAAAAQAAADQGAAA=");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static final class MyThread extends Thread {
    public MyThread(CountDownLatch delay, int id) {
      super("Thread: " + id);
      this.thr_id = id;
      this.results = new HashSet<>();
      this.finish = false;
      this.delay = delay;
    }

    public void run() {
      delay.countDown();
      while (!finish) {
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
        if (!s.equals("Hello from " + getName())
            && !s.equals("Hello, " + DEFAULT_VAL + ", " + DEFAULT_VAL + ", " + DEFAULT_VAL
                + " from " + getName())
            && !s.equals(
                "Hello World, Bonjour le Monde, Hej Verden, こんにちは世界 from " + getName())) {
          System.out.println("FAIL " + thr_id + ": Unexpected result: " + s);
        }
      }
    }

    public HashSet<String> results;
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

  public static void doRedefinition() throws Exception {
    // Get the current set of fields.
    Field[] fields = Transform.class.getDeclaredFields();
    // Get all the threads in the 'main' thread group
    ThreadGroup mytg = Thread.currentThread().getThreadGroup();
    Thread[] all_threads = new Thread[mytg.activeCount()];
    mytg.enumerate(all_threads);
    Set<Thread> thread_set = new HashSet<>(Arrays.asList(all_threads));
    // We don't want to suspend ourself, that would cause a deadlock.
    thread_set.remove(Thread.currentThread());
    // If some of the other threads finished between calling mytg.activeCount and enumerate we will
    // have nulls. These nulls are interpreted as currentThread by SuspendThreadList so we want to
    // get rid of them.
    thread_set.remove(null);
    // Suspend them.
    Suspension.suspendList(thread_set.toArray(new Thread[0]));
    // Actual redefine.
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    // Get the new fields.
    Field[] new_fields = Transform.class.getDeclaredFields();
    Set<Field> field_set = new HashSet(Arrays.asList(new_fields));
    field_set.removeAll(Arrays.asList(fields));
    // Initialize the new fields on the old objects and resume.
    UpdateFieldValuesAndResumeThreads(thread_set.toArray(new Thread[0]),
        Transform.class,
        field_set.toArray(new Field[0]),
        DEFAULT_VAL);
  }

  public static void doTest() throws Exception {
    // Force the Transform class to be initialized. We are suspending the remote
    // threads so if one of them is in the class initialization (and therefore
    // has a monitor lock on the class object) the redefinition will deadlock
    // waiting for the clinit to finish and the monitor to be released.
    if (null == Class.forName("art.Test2005$Transform")) {
      throw new Error("No class!");
    }
    MyThread[] threads = startThreads(NUM_THREADS);

    doRedefinition();
    finishThreads(threads);
  }
  public static native void UpdateFieldValuesAndResumeThreads(
      Thread[] t, Class<?> redefined_class, Field[] new_fields, String default_val);
}
