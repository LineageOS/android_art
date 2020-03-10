/*
 * Copyright (C) 2020 The Android Open Source Project
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

public class Test2031 {
  public static class MyClass {
    public void starting() {
      System.out.println("Starting up!");
    }
    public String toString() {
      return "This is my object!";
    }
  }

  public static void $noinline$doSimulateZygoteFork() {
    simulateZygoteFork();
  }

  public static void $noinline$run(String testdir) throws Exception {
    $noinline$doSimulateZygoteFork();
    final MyClass myObject = new MyClass();
    $noinline$startTest(testdir, myObject);
    System.out.println(myObject);
  }

  public static void $noinline$startTest(String testdir, final MyClass myObject) throws Exception {
    Thread thr = new Thread(() -> {
      try {
        waitForNativeSleep();
        setupJvmti(testdir);
        Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
        Thread tester = new Thread(() -> {
          try {
            myObject.starting();
            doTest();
          } catch (Exception e) {
            throw new Error("Failure!", e);
          }
        });
        tester.start();
        tester.join();
        wakeupNativeSleep();
      } catch (Exception e) {
        throw new Error("Failure!", e);
      }
    });
    thr.start();
    nativeSleep();
    thr.join();
  }

  public static native void simulateZygoteFork();
  public static native void setupJvmti(String testdir);
  public static native void waitForNativeSleep();
  public static native void wakeupNativeSleep();
  public static native void nativeSleep();

  private static void doRedefinition() {
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, REDEFINED_DEX_BYTES);
  }

  public static class SuperTransform {
    public int id;

    public SuperTransform(int id) {
      this.id = id;
    }

    public String toString() {
      return "SuperTransform { id: " + id + ", class: " + getClass() + " }";
    }
  }

  public static class Transform extends SuperTransform {
    static {
    }

    public static Object BAR =
        new Object() {
          public String toString() {
            return "value of <" + this.get() + ">";
          }

          public Object get() {
            return "BAR FIELD";
          }
        };
    public static Object FOO =
        new Object() {
          public String toString() {
            return "value of <" + this.get() + ">";
          }

          public Object get() {
            return "FOO FIELD";
          }
        };
    // This class has no virtual fields or methods. This means we can structurally redefine it
    // without having to change the size of any instances.
    public Transform(int id) {
      super(id);
    }

    public static String staticToString() {
      return Transform.class.toString() + "[FOO: " + FOO + ", BAR: " + BAR + "]";
    }
  }

  public static class SubTransform extends Transform {
    public SubTransform(int id) {
      super(id);
    }

    public String myToString() {
      return "SubTransform (subclass of: " + staticToString() + ") { id: " + id + " }";
    }
  }

  /* Base64 encoded class of:
   * public static class Transform extends SuperTransform {
   *   static {}
   *   public Transform(int id) { super(id + 1000); }
   *   // NB This is the order the fields will be laid out in memory.
   *   public static Object BAR;
   *   public static Object BAZ;
   *   public static Object FOO;
   *   public static String staticToString() {
   *    return Transform.class.toString() + "[FOO: " + FOO + ", BAR: " + BAR + ", BAZ: " + BAZ + "]";
   *   }
   * }
   */
  private static byte[] REDEFINED_DEX_BYTES =
      Base64.getDecoder()
          .decode(
"ZGV4CjAzNQC78lC18jI6omumTaKUcf/8pvcR4/Hx2u3QBQAAcAAAAHhWNBIAAAAAAAAAAAwFAAAg" +
"AAAAcAAAAAsAAADwAAAABQAAABwBAAADAAAAWAEAAAkAAABwAQAAAQAAALgBAAD4AwAA2AEAAKoC" +
"AACzAgAAvAIAAMYCAADOAgAA0wIAANgCAADdAgAA4AIAAOMCAADnAgAABgMAACADAAAwAwAAVAMA" +
"AHQDAACHAwAAmwMAAK8DAADKAwAA2QMAAOQDAADnAwAA6wMAAPMDAAD2AwAAAwQAAAsEAAARBAAA" +
"IQQAACsEAAAyBAAABwAAAAoAAAALAAAADAAAAA0AAAAOAAAADwAAABAAAAARAAAAEgAAABUAAAAI" +
"AAAACAAAAAAAAAAJAAAACQAAAJQCAAAJAAAACQAAAJwCAAAVAAAACgAAAAAAAAAWAAAACgAAAKQC" +
"AAACAAcABAAAAAIABwAFAAAAAgAHAAYAAAABAAQAAwAAAAIAAwACAAAAAgAEAAMAAAACAAAAHAAA" +
"AAYAAAAdAAAACQADAAMAAAAJAAEAGgAAAAkAAgAaAAAACQAAAB0AAAACAAAAAQAAAAEAAAAAAAAA" +
"EwAAAPwEAADQBAAAAAAAAAUAAAACAAAAjQIAADYAAAAcAAIAbhAEAAAADABiAQIAYgIAAGIDAQAi" +
"BAkAcBAFAAQAbiAHAAQAGgAXAG4gBwAEAG4gBgAUABoAAABuIAcABABuIAYAJAAaAAEAbiAHAAQA" +
"biAGADQAGgAYAG4gBwAEAG4QCAAEAAwAEQAAAAAAAAAAAIQCAAABAAAADgAAAAIAAgACAAAAiAIA" +
"AAYAAADQEegDcCAAABAADgAPAA4AEAEADgAWAA4AAAAAAQAAAAcAAAABAAAACAAAAAEAAAAAAAcs" +
"IEJBUjogAAcsIEJBWjogAAg8Y2xpbml0PgAGPGluaXQ+AANCQVIAA0JBWgADRk9PAAFJAAFMAAJM" +
"TAAdTGFydC9UZXN0MjAzMSRTdXBlclRyYW5zZm9ybTsAGExhcnQvVGVzdDIwMzEkVHJhbnNmb3Jt" +
"OwAOTGFydC9UZXN0MjAzMTsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxk" +
"YWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwARTGphdmEvbGFuZy9DbGFzczsAEkxqYXZhL2xh" +
"bmcvT2JqZWN0OwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7" +
"AA1UZXN0MjAzMS5qYXZhAAlUcmFuc2Zvcm0AAVYAAlZJAAZbRk9POiAAAV0AC2FjY2Vzc0ZsYWdz" +
"AAZhcHBlbmQABG5hbWUADnN0YXRpY1RvU3RyaW5nAAh0b1N0cmluZwAFdmFsdWUAjAF+fkQ4eyJj" +
"b21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJoYXMtY2hlY2tzdW1zIjpmYWxzZSwibWluLWFwaSI6" +
"MSwic2hhLTEiOiJkMWQ1MWMxY2IzZTg1YWEzMGUwMGE2ODIyY2NhODNiYmUxZGZlOTQ1IiwidmVy" +
"c2lvbiI6IjIuMC4xMy1kZXYifQACBAEeGAMCBQIZBAkbFxQDAAMAAAkBCQEJAYiABNQEAYGABOgE" +
"AQnYAwAAAAAAAAIAAADBBAAAxwQAAPAEAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAEAAAAAAAAAAQAA" +
"ACAAAABwAAAAAgAAAAsAAADwAAAAAwAAAAUAAAAcAQAABAAAAAMAAABYAQAABQAAAAkAAABwAQAA" +
"BgAAAAEAAAC4AQAAASAAAAMAAADYAQAAAyAAAAMAAACEAgAAARAAAAMAAACUAgAAAiAAACAAAACq" +
"AgAABCAAAAIAAADBBAAAACAAAAEAAADQBAAAAxAAAAIAAADsBAAABiAAAAEAAAD8BAAAABAAAAEA" +
"AAAMBQAA");

  public static void doTest() throws Exception {
    Transform t1 = new Transform(1);
    SuperTransform t2 = new SubTransform(2);
    doRedefinition();
  }
}
