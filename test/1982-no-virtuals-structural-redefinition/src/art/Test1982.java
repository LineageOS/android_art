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

public class Test1982 {
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
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, REDEFINED_DEX_BYTES);
  }

  private static void readReflective(String msg, Field[] fields, Object recv) throws Exception {
    System.out.println(msg);
    for (Field f : fields) {
      System.out.println(
          f.toString() + " on " + printGeneric(recv) + " = " + printGeneric(f.get(recv)));
    }
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
              "ZGV4CjAzNQAV5GctNSI+SEKJDaJIQLEac9ClAxZUSZq4BQAAcAAAAHhWNBIAAAAAAAAAAPQEAAAg"
                  + "AAAAcAAAAAsAAADwAAAABQAAABwBAAADAAAAWAEAAAkAAABwAQAAAQAAALgBAADgAwAA2AEAAKoC"
                  + "AACzAgAAvAIAAMYCAADOAgAA0wIAANgCAADdAgAA4AIAAOMCAADnAgAABgMAACADAAAwAwAAVAMA"
                  + "AHQDAACHAwAAmwMAAK8DAADKAwAA2QMAAOQDAADnAwAA6wMAAPMDAAD2AwAAAwQAAAsEAAARBAAA"
                  + "IQQAACsEAAAyBAAABwAAAAoAAAALAAAADAAAAA0AAAAOAAAADwAAABAAAAARAAAAEgAAABUAAAAI"
                  + "AAAACAAAAAAAAAAJAAAACQAAAJQCAAAJAAAACQAAAJwCAAAVAAAACgAAAAAAAAAWAAAACgAAAKQC"
                  + "AAACAAcABAAAAAIABwAFAAAAAgAHAAYAAAABAAQAAwAAAAIAAwACAAAAAgAEAAMAAAACAAAAHAAA"
                  + "AAYAAAAdAAAACQADAAMAAAAJAAEAGgAAAAkAAgAaAAAACQAAAB0AAAACAAAAAQAAAAEAAAAAAAAA"
                  + "EwAAAOQEAAC5BAAAAAAAAAUAAAACAAAAjQIAADYAAAAcAAIAbhAEAAAADABiAQIAYgIAAGIDAQAi"
                  + "BAkAcBAFAAQAbiAHAAQAGgAXAG4gBwAEAG4gBgAUABoAAABuIAcABABuIAYAJAAaAAEAbiAHAAQA"
                  + "biAGADQAGgAYAG4gBwAEAG4QCAAEAAwAEQAAAAAAAAAAAIQCAAABAAAADgAAAAIAAgACAAAAiAIA"
                  + "AAYAAADQEegDcCAAABAADgAKAA4ACwEADgARAA4AAAAAAQAAAAcAAAABAAAACAAAAAEAAAAAAAcs"
                  + "IEJBUjogAAcsIEJBWjogAAg8Y2xpbml0PgAGPGluaXQ+AANCQVIAA0JBWgADRk9PAAFJAAFMAAJM"
                  + "TAAdTGFydC9UZXN0MTk4MiRTdXBlclRyYW5zZm9ybTsAGExhcnQvVGVzdDE5ODIkVHJhbnNmb3Jt"
                  + "OwAOTGFydC9UZXN0MTk4MjsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxk"
                  + "YWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwARTGphdmEvbGFuZy9DbGFzczsAEkxqYXZhL2xh"
                  + "bmcvT2JqZWN0OwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7"
                  + "AA1UZXN0MTk4Mi5qYXZhAAlUcmFuc2Zvcm0AAVYAAlZJAAZbRk9POiAAAV0AC2FjY2Vzc0ZsYWdz"
                  + "AAZhcHBlbmQABG5hbWUADnN0YXRpY1RvU3RyaW5nAAh0b1N0cmluZwAFdmFsdWUAdn5+RDh7ImNv"
                  + "bXBpbGF0aW9uLW1vZGUiOiJkZWJ1ZyIsIm1pbi1hcGkiOjEsInNoYS0xIjoiYTgzNTJmMjU0ODg1"
                  + "MzYyY2NkOGQ5MDlkMzUyOWM2MDA5NGRkODk2ZSIsInZlcnNpb24iOiIxLjYuMjAtZGV2In0AAgQB"
                  + "HhgDAgUCGQQJGxcUAwADAAAJAQkBCQGIgATUBAGBgAToBAEJ2AMAAAAAAAIAAACqBAAAsAQAANgE"
                  + "AAAAAAAAAAAAAAAAAAAQAAAAAAAAAAEAAAAAAAAAAQAAACAAAABwAAAAAgAAAAsAAADwAAAAAwAA"
                  + "AAUAAAAcAQAABAAAAAMAAABYAQAABQAAAAkAAABwAQAABgAAAAEAAAC4AQAAASAAAAMAAADYAQAA"
                  + "AyAAAAMAAACEAgAAARAAAAMAAACUAgAAAiAAACAAAACqAgAABCAAAAIAAACqBAAAACAAAAEAAAC5"
                  + "BAAAAxAAAAIAAADUBAAABiAAAAEAAADkBAAAABAAAAEAAAD0BAAA");

  public static void doTest() throws Exception {
    Transform t1 = new Transform(1);
    SuperTransform t2 = new SubTransform(2);
    readReflective("Reading with reflection.", Transform.class.getDeclaredFields(), null);
    readReflective(
        "Reading with reflection on subtransform instance.", SubTransform.class.getFields(), t2);
    System.out.println("Reading normally.");
    System.out.println("Read BAR field: " + printGeneric(Transform.BAR));
    System.out.println("Read FOO field: " + printGeneric(Transform.FOO));
    System.out.println("t1 is " + printGeneric(t1));
    System.out.println("t2 is " + printGeneric(t2));
    doRedefinition();
    System.out.println("Redefined: " + Transform.staticToString());
    readReflective(
        "Reading with reflection after redefinition.", Transform.class.getDeclaredFields(), null);
    readReflective(
        "Reading with reflection after redefinition on subtransform instance.",
        SubTransform.class.getFields(),
        t2);
    System.out.println("Reading normally after possible modification.");
    System.out.println("Read FOO field: " + printGeneric(Transform.FOO));
    System.out.println("Read BAR field: " + printGeneric(Transform.BAR));
    System.out.println("t1 is " + printGeneric(t1));
    System.out.println("t2 is " + printGeneric(t2));
    SubTransform t3 = new SubTransform(3);
    System.out.println("new SubTransform is " + printGeneric(t3));
    System.out.println("myToString of " + printGeneric(t3) + " is " + t3.myToString());
    // We verified in test 1980 that getDeclaredConstructor will throw if the class is obsolete.
    // This therefore is a reasonable test that the t1 object's declaring class was updated.
    System.out.println(
        "Creating new transform from t1 class = "
            + printGeneric(t1.getClass().getDeclaredConstructor(Integer.TYPE).newInstance(4)));
  }
}
