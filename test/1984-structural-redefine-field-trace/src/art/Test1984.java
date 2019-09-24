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

import java.lang.reflect.Executable;
import java.lang.reflect.Field;
import java.util.Base64;

public class Test1984 {
  public static void notifyFieldModify(
      Executable method, long location, Class<?> f_klass, Object target, Field f, Object value) {
    System.out.println("method: " + method + "\tMODIFY: " + f + "\tSet to: " + value);
  }

  public static void notifyFieldAccess(
      Executable method, long location, Class<?> f_klass, Object target, Field f) {
    System.out.println("method: " + method + "\tACCESS: " + f);
  }

  public static class Transform {
    public static int count_down = 2;
    public static boolean boom = false;
    public static boolean tock = false;

    public static void tick() {
      boolean tocked = tock;
      tock = !tock;
      if (tocked) {
        count_down--;
      }
      if (count_down == 0) {
        boom = true;
      }
    }
  }

  /* Base64 encoded dex file for.
   * // NB The addition of aaa_INITIAL means the fields all have different offsets
   * public static class Transform {
   *   public static int aaa_INITIAL = 0;
   *   public static int count_down = 2;
   *   public static boolean boom = false;
   *   public static boolean tock = false;
   *   public static void tick() {
   *     boolean tocked = tock;
   *     tock = !tock;
   *     if (tocked) {
   *       count_down--;
   *     }
   *     if (count_down == 0) {
   *       boom = true;
   *     }
   *   }
   * }
   */
  public static final byte[] REDEFINED_DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQDejZufbnVbJEn1/OfB3XmJPtVbudlWkvnsAwAAcAAAAHhWNBIAAAAAAAAAADQDAAAU"
                  + "AAAAcAAAAAgAAADAAAAAAQAAAOAAAAAEAAAA7AAAAAQAAAAMAQAAAQAAACwBAACgAgAATAEAAPAB"
                  + "AAD6AQAAAgIAAAUCAAAfAgAALwIAAFMCAABzAgAAhwIAAJYCAAChAgAApAIAAKcCAAC0AgAAwQIA"
                  + "AMcCAADTAgAA2QIAAN8CAADlAgAAAgAAAAMAAAAEAAAABQAAAAYAAAAHAAAACgAAAAsAAAAKAAAA"
                  + "BgAAAAAAAAABAAAADAAAAAEABwAOAAAAAQAAAA8AAAABAAcAEgAAAAEAAAAAAAAAAQAAAAEAAAAB"
                  + "AAAAEQAAAAUAAAABAAAAAQAAAAEAAAAFAAAAAAAAAAgAAADgAQAAFgMAAAAAAAACAAAABwMAAA0D"
                  + "AAACAAAAAAAAAOwCAAALAAAAEgFnAQAAEiBnAAIAagEBAGoBAwAOAAAAAQABAAEAAAD0AgAABAAA"
                  + "AHAQAwAAAA4AAwAAAAAAAAD5AgAAGwAAABIRYwIDAGMAAwA5ABQAARBqAAMAOAIIAGAAAgDYAAD/"
                  + "ZwACAGAAAgA5AAQAagEBAA4AEgAo7gAATAEAAAAAAAAAAAAAAAAAAAg8Y2xpbml0PgAGPGluaXQ+"
                  + "AAFJABhMYXJ0L1Rlc3QxOTg0JFRyYW5zZm9ybTsADkxhcnQvVGVzdDE5ODQ7ACJMZGFsdmlrL2Fu"
                  + "bm90YXRpb24vRW5jbG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90YXRpb24vSW5uZXJDbGFzczsA"
                  + "EkxqYXZhL2xhbmcvT2JqZWN0OwANVGVzdDE5ODQuamF2YQAJVHJhbnNmb3JtAAFWAAFaAAthYWFf"
                  + "SU5JVElBTAALYWNjZXNzRmxhZ3MABGJvb20ACmNvdW50X2Rvd24ABG5hbWUABHRpY2sABHRvY2sA"
                  + "BXZhbHVlAAgABx0tPC0ABwAHDgANAAcdLXgtaksuAnkdAAIDARMYAgIEAg0ECRAXCQQAAwAACQEJ"
                  + "AQkBCQCIgATYAgGBgASAAwEJmAMAAA8AAAAAAAAAAQAAAAAAAAABAAAAFAAAAHAAAAACAAAACAAA"
                  + "AMAAAAADAAAAAQAAAOAAAAAEAAAABAAAAOwAAAAFAAAABAAAAAwBAAAGAAAAAQAAACwBAAADEAAA"
                  + "AQAAAEwBAAABIAAAAwAAAFgBAAAGIAAAAQAAAOABAAACIAAAFAAAAPABAAADIAAAAwAAAOwCAAAE"
                  + "IAAAAgAAAAcDAAAAIAAAAQAAABYDAAAAEAAAAQAAADQDAAA=");

  public static void run() throws Exception {
    System.out.println("Dumping fields at start");
    for (Field f : Transform.class.getDeclaredFields()) {
      System.out.println(f.toString() + "=" + f.get(null));
    }
    Trace.disableTracing(Thread.currentThread());
    Trace.enableFieldTracing(
        Test1984.class,
        Test1984.class.getDeclaredMethod(
            "notifyFieldAccess",
            Executable.class,
            Long.TYPE,
            Class.class,
            Object.class,
            Field.class),
        Test1984.class.getDeclaredMethod(
            "notifyFieldModify",
            Executable.class,
            Long.TYPE,
            Class.class,
            Object.class,
            Field.class,
            Object.class),
        Thread.currentThread());
    for (Field f : Transform.class.getDeclaredFields()) {
      Trace.watchFieldAccess(f);
      Trace.watchFieldModification(f);
    }
    // count_down = 2
    Transform.tick(); // count_down = 2
    Transform.tick(); // count_down = 1
    System.out.println("REDEFINING TRANSFORM CLASS");
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, REDEFINED_DEX_BYTES);
    Transform.tick(); // count_down = 1
    Transform.tick(); // count_down = 0
    System.out.println("Dumping fields at end");
    for (Field f : Transform.class.getDeclaredFields()) {
      System.out.println(f.toString() + "=" + f.get(null));
    }
    // Turn off tracing so we don't have to deal with print internals.
    Trace.disableTracing(Thread.currentThread());
  }
}
