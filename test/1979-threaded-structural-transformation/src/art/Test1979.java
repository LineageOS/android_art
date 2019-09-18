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

import java.lang.ref.*;
import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.CountDownLatch;
import java.util.function.Supplier;

public class Test1979 {
  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  private static final boolean PRINT_NONDETERMINISTIC = false;

  public static WeakHashMap<Object, Long> id_nums = new WeakHashMap<>();
  public static long next_id = 0;

  public static String printGeneric(Object o) {
    Long id = id_nums.get(o);
    if (id == null) {
      id = Long.valueOf(next_id++);
      id_nums.put(o, id);
    }
    if (o == null) {
      return "(ID: " + id + ") <NULL>";
    }
    Class oc = o.getClass();
    if (oc.isArray() && oc.getComponentType() == Byte.TYPE) {
      return "(ID: "
          + id
          + ") "
          + Arrays.toString(Arrays.copyOf((byte[]) o, 10)).replace(']', ',')
          + " ...]";
    } else {
      return "(ID: " + id + ") " + o.toString();
    }
  }

  private static void doRedefinition() {
    Redefinition.doCommonStructuralClassRedefinition(
        Transform.class, REDEFINED_DEX_BYTES);
  }

  private static void readReflective(String msg) throws Exception {
    System.out.println(msg);
    for (Field f : Transform.class.getFields()) {
      System.out.println(f.toString() + " = " + printGeneric(f.get(null)));
    }
  }

  public static class Transform {
    static {}
    public static Object BAR = new Object() {
      public String toString() {
        return "value of <" + this.get() + ">";
      }
      public Object get() {
        return "BAR FIELD";
      }
    };
    public static Object FOO = new Object() {
      public String toString() {
        return "value of <" + this.get() + ">";
      }
      public Object get() {
        return "FOO FIELD";
      }
    };
    public static String staticToString() {
      return Transform.class.toString() + "[FOO: " + FOO + ", BAR: " + BAR + "]";
    }
  }

  /* Base64 encoded class of:
   * public static class Transform {
   *   static {}
   *   // NB This is the order the fields will be laid out in memory.
   *   public static Object BAR;
   *   public static Object BAZ;
   *   public static Object FOO;
   *   public static String staticToString() {
   *    return Transform.class.toString() + "[FOO: " + FOO + ", BAR: " + BAR + ", BAZ: " + BAZ + "]";
   *   }
   * }
   */
  private static byte[] REDEFINED_DEX_BYTES = Base64.getDecoder().decode(
      "ZGV4CjAzNQDrznAlv8Fs6FNeDAHAxiU9uy8DUayd82ZkBQAAcAAAAHhWNBIAAAAAAAAAAKAEAAAd" +
      "AAAAcAAAAAkAAADkAAAABAAAAAgBAAADAAAAOAEAAAkAAABQAQAAAQAAAJgBAACsAwAAuAEAAHoC" +
      "AACDAgAAjAIAAJYCAACeAgAAowIAAKgCAACtAgAAsAIAALQCAADOAgAA3gIAAAIDAAAiAwAANQMA" +
      "AEkDAABdAwAAeAMAAIcDAACSAwAAlQMAAJ0DAACgAwAArQMAALUDAAC7AwAAywMAANUDAADcAwAA" +
      "CQAAAAoAAAALAAAADAAAAA0AAAAOAAAADwAAABAAAAATAAAABwAAAAYAAAAAAAAACAAAAAcAAABs" +
      "AgAACAAAAAcAAAB0AgAAEwAAAAgAAAAAAAAAAAAFAAQAAAAAAAUABQAAAAAABQAGAAAAAAADAAIA" +
      "AAAAAAMAAwAAAAAAAAAZAAAABAAAABoAAAAFAAMAAwAAAAcAAwADAAAABwABABcAAAAHAAIAFwAA" +
      "AAcAAAAaAAAAAAAAAAEAAAAFAAAAAAAAABEAAACQBAAAYwQAAAAAAAAFAAAAAgAAAGgCAAA2AAAA" +
      "HAAAAG4QAwAAAAwAYgECAGICAABiAwEAIgQHAHAQBQAEAG4gBwAEABoAFABuIAcABABuIAYAFAAa" +
      "AAAAbiAHAAQAbiAGACQAGgABAG4gBwAEAG4gBgA0ABoAFQBuIAcABABuEAgABAAMABEAAAAAAAAA" +
      "AABgAgAAAQAAAA4AAAABAAEAAQAAAGQCAAAEAAAAcBAEAAAADgAIAA4ABwAOAA4ADgABAAAABQAA" +
      "AAEAAAAGAAcsIEJBUjogAAcsIEJBWjogAAg8Y2xpbml0PgAGPGluaXQ+AANCQVIAA0JBWgADRk9P" +
      "AAFMAAJMTAAYTGFydC9UZXN0MTk3OSRUcmFuc2Zvcm07AA5MYXJ0L1Rlc3QxOTc5OwAiTGRhbHZp" +
      "ay9hbm5vdGF0aW9uL0VuY2xvc2luZ0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xh" +
      "c3M7ABFMamF2YS9sYW5nL0NsYXNzOwASTGphdmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0" +
      "cmluZzsAGUxqYXZhL2xhbmcvU3RyaW5nQnVpbGRlcjsADVRlc3QxOTc5LmphdmEACVRyYW5zZm9y" +
      "bQABVgAGW0ZPTzogAAFdAAthY2Nlc3NGbGFncwAGYXBwZW5kAARuYW1lAA5zdGF0aWNUb1N0cmlu" +
      "ZwAIdG9TdHJpbmcABXZhbHVlAHZ+fkQ4eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJtaW4t" +
      "YXBpIjoxLCJzaGEtMSI6ImE4MzUyZjI1NDg4NTM2MmNjZDhkOTA5ZDM1MjljNjAwOTRkZDg5NmUi" +
      "LCJ2ZXJzaW9uIjoiMS42LjIwLWRldiJ9AAICARsYAQIDAhYECRgXEgMAAwAACQEJAQkAiIAEtAQB" +
      "gYAEyAQBCbgDAAAAAAAAAAIAAABUBAAAWgQAAIQEAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAEAAAAA" +
      "AAAAAQAAAB0AAABwAAAAAgAAAAkAAADkAAAAAwAAAAQAAAAIAQAABAAAAAMAAAA4AQAABQAAAAkA" +
      "AABQAQAABgAAAAEAAACYAQAAASAAAAMAAAC4AQAAAyAAAAMAAABgAgAAARAAAAIAAABsAgAAAiAA" +
      "AB0AAAB6AgAABCAAAAIAAABUBAAAACAAAAEAAABjBAAAAxAAAAIAAACABAAABiAAAAEAAACQBAAA" +
      "ABAAAAEAAACgBAAA");

  public interface TRunnable {
    public void run() throws Exception;
  }

  public static void doTest() throws Exception {
    final CountDownLatch cdl = new CountDownLatch(1);
    final CountDownLatch continueLatch = new CountDownLatch(1);
    // Make sure the transformed class is already loaded before we start running (and possibly
    // compiling) the test thread.
    System.out.println("Hitting class " + Transform.staticToString());
    Thread t = new Thread(() -> {
      try {
        // We don't want to read these in the same method here to ensure that no reference to
        // Transform is active on this thread at the time the redefinition occurs. To accomplish
        // this just run the code in a different method, which is good enough.
        ((TRunnable)() -> {
          System.out.println("Initial: " + Transform.staticToString());
          readReflective("Reading with reflection.");
          System.out.println("Reading normally.");
          System.out.println("Read BAR field: " + printGeneric(Transform.BAR));
          System.out.println("Read FOO field: " + printGeneric(Transform.FOO));
        }).run();
        cdl.countDown();
        continueLatch.await();
        // Now that redefinition has occurred without this frame having any references to the
        // Transform class we want to make sure we have the correct offsets.
        System.out.println("Redefined: " + Transform.staticToString());
        readReflective("Reading with reflection after possible modification.");
        System.out.println("Reading normally after possible modification.");
        System.out.println("Read FOO field: " + printGeneric(Transform.FOO));
        System.out.println("Read BAR field: " + printGeneric(Transform.BAR));
      } catch (Exception e) {
        throw new Error(e);
      }
    });
    t.start();
    cdl.await();
    doRedefinition();
    continueLatch.countDown();
    t.join();
  }
}
