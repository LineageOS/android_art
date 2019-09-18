/*
 * Copyright (C) 2017 The Android Open Source Project
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

import art.*;
import java.lang.ref.*;
import java.lang.reflect.*;
import java.util.*;
import java.util.function.Consumer;
import sun.misc.Unsafe;

public class Main {
  public static class Transform {
    static {
    }

    public static Object SECRET_ARRAY = new byte[] {1, 2, 3, 4};
    public static long SECRET_NUMBER = 42;

    public static void foo() {}
  }

  /* Base64 for
   * public static class Trasform {
   *   static {}
   *   public static Object AAA_PADDING;
   *   public static Object SECRET_ARRAY;
   *   public static long SECRET_NUMBER;
   *   public static void foo() {}
   *   public static void bar() {}
   * }
   */
  public static final byte[] REDEFINED_DEX_FILE =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQDdmsOAlizFD4Ogb6+/mfSdVzhmL8e/mRcYBAAAcAAAAHhWNBIAAAAAAAAAAGADAAAU"
                  + "AAAAcAAAAAcAAADAAAAAAQAAANwAAAADAAAA6AAAAAUAAAAAAQAAAQAAACgBAADQAgAASAEAAKwB"
                  + "AAC2AQAAvgEAAMsBAADOAQAA4AEAAOgBAAAMAgAALAIAAEACAABLAgAAWQIAAGgCAABzAgAAdgIA"
                  + "AIMCAACIAgAAjQIAAJMCAACaAgAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAADQAAAA0AAAAGAAAA"
                  + "AAAAAAEABQACAAAAAQAFAAoAAAABAAAACwAAAAEAAAAAAAAAAQAAAAEAAAABAAAADwAAAAEAAAAQ"
                  + "AAAABQAAAAEAAAABAAAAAQAAAAUAAAAAAAAACQAAAFADAAAhAwAAAAAAAAAAAAAAAAAAmgEAAAEA"
                  + "AAAOAAAAAQABAAEAAACeAQAABAAAAHAQBAAAAA4AAAAAAAAAAACiAQAAAQAAAA4AAAAAAAAAAAAA"
                  + "AKYBAAABAAAADgAHAA4ABgAOAAsADgAKAA4AAAAIPGNsaW5pdD4ABjxpbml0PgALQUFBX1BBRERJ"
                  + "TkcAAUoAEExNYWluJFRyYW5zZm9ybTsABkxNYWluOwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xv"
                  + "c2luZ0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xhc3M7ABJMamF2YS9sYW5nL09i"
                  + "amVjdDsACU1haW4uamF2YQAMU0VDUkVUX0FSUkFZAA1TRUNSRVRfTlVNQkVSAAlUcmFuc2Zvcm0A"
                  + "AVYAC2FjY2Vzc0ZsYWdzAANiYXIAA2ZvbwAEbmFtZQAFdmFsdWUAdn5+RDh7ImNvbXBpbGF0aW9u"
                  + "LW1vZGUiOiJkZWJ1ZyIsIm1pbi1hcGkiOjEsInNoYS0xIjoiYTgzNTJmMjU0ODg1MzYyY2NkOGQ5"
                  + "MDlkMzUyOWM2MDA5NGRkODk2ZSIsInZlcnNpb24iOiIxLjYuMjAtZGV2In0AAgMBEhgCAgQCDgQJ"
                  + "ERcMAwAEAAAJAQkBCQCIgATIAgGBgATcAgEJ9AIBCYgDAAAAAAACAAAAEgMAABgDAABEAwAAAAAA"
                  + "AAAAAAAAAAAADwAAAAAAAAABAAAAAAAAAAEAAAAUAAAAcAAAAAIAAAAHAAAAwAAAAAMAAAABAAAA"
                  + "3AAAAAQAAAADAAAA6AAAAAUAAAAFAAAAAAEAAAYAAAABAAAAKAEAAAEgAAAEAAAASAEAAAMgAAAE"
                  + "AAAAmgEAAAIgAAAUAAAArAEAAAQgAAACAAAAEgMAAAAgAAABAAAAIQMAAAMQAAACAAAAQAMAAAYg"
                  + "AAABAAAAUAMAAAAQAAABAAAAYAMAAA==");

  private interface TConsumer<T> {
    public void accept(T t) throws Exception;
  }

  private interface ResetIterator<T> extends Iterator<T> {
    public void reset();
  }

  private static final class BaseResetIter implements ResetIterator<Object[]> {
    private boolean have_next = true;

    public Object[] next() {
      if (have_next) {
        have_next = false;
        return new Object[0];
      } else {
        throw new NoSuchElementException("only one element");
      }
    }

    public boolean hasNext() {
      return have_next;
    }

    public void reset() {
      have_next = true;
    }
  }

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);

    // Get the Unsafe object.
    Field f = Unsafe.class.getDeclaredField("THE_ONE");
    f.setAccessible(true);
    Unsafe u = (Unsafe) f.get(null);

    // Get the offsets into the original Transform class of the fields
    long off_secret_array = genericFieldOffset(Transform.class.getDeclaredField("SECRET_ARRAY"));
    long off_secret_number = genericFieldOffset(Transform.class.getDeclaredField("SECRET_NUMBER"));

    System.out.println("Reading normally.");
    System.out.println("\tOriginal secret number is: " + Transform.SECRET_NUMBER);
    System.out.println("\tOriginal secret array is: " + Arrays.toString((byte[])Transform.SECRET_ARRAY));
    System.out.println("Using unsafe to access values directly from memory.");
    System.out.println(
        "\tOriginal secret number is: " + u.getLong(Transform.class, off_secret_number));
    System.out.println(
        "\tOriginal secret array is: "
            + Arrays.toString((byte[]) u.getObject(Transform.class, off_secret_array)));

    // Redefine in a way that changes the offsets.
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, REDEFINED_DEX_FILE);

    // Make sure the value is the same.
    System.out.println("Reading normally post redefinition.");
    System.out.println("\tPost-redefinition secret number is: " + Transform.SECRET_NUMBER);
    System.out.println("\tPost-redefinition secret array is: " + Arrays.toString((byte[])Transform.SECRET_ARRAY));

    // Get the (old) obsolete class from the ClassExt
    Field ext_field = Class.class.getDeclaredField("extData");
    ext_field.setAccessible(true);
    Object ext_data = ext_field.get(Transform.class);
    Field oc_field = ext_data.getClass().getDeclaredField("obsoleteClass");
    oc_field.setAccessible(true);
    Class<?> obsolete_class = (Class<?>) oc_field.get(ext_data);

    // Try reading the fields directly out of memory using unsafe.
    System.out.println("Obsolete class is: " + obsolete_class);
    System.out.println("Using unsafe to access obsolete values directly from memory.");
    System.out.println(
        "\tObsolete secret number is: " + u.getLong(obsolete_class, off_secret_number));
    System.out.println(
        "\tObsolete secret array is: "
            + Arrays.toString((byte[]) u.getObject(obsolete_class, off_secret_array)));

    // Try calling all the public, non-static methods on the obsolete class. Make sure we cannot get
    // j.l.r.{Method,Field} objects or instances.
    TConsumer<Class> cc =
        (Class c) -> {
          for (Method m : Class.class.getDeclaredMethods()) {
            if (Modifier.isPublic(m.getModifiers()) && !Modifier.isStatic(m.getModifiers())) {
              Iterable<Object[]> iter = CollectParameterValues(m, obsolete_class);
              System.out.println("Calling " + m + " with params: " + iter);
              for (Object[] arr : iter) {
                try {
                  System.out.println(
                      m
                          + " on "
                          + safePrint(c)
                          + " with "
                          + deepPrint(arr)
                          + " = "
                          + safePrint(m.invoke(c, arr)));
                } catch (Throwable e) {
                  System.out.println(
                      m + " with " + deepPrint(arr) + " throws " + safePrint(e) + ": " + safePrint(e.getCause()));
                }
              }
            }
          }
        };
    System.out.println("\n\nUsing obsolete class object!\n\n");
    cc.accept(obsolete_class);
    System.out.println("\n\nUsing non-obsolete class object!\n\n");
    cc.accept(Transform.class);
  }

  public static Iterable<Object[]> CollectParameterValues(Method m, Class<?> obsolete_class) throws Exception {
    Class<?>[] types = m.getParameterTypes();
    final Object[][] params = new Object[types.length][];
    for (int i = 0; i < types.length; i++) {
      if (types[i].equals(Class.class)) {
        params[i] =
            new Object[] {
              null, Object.class, obsolete_class, Transform.class, Long.TYPE, Class.class
            };
      } else if (types[i].equals(Boolean.TYPE)) {
        params[i] = new Object[] {Boolean.TRUE, Boolean.FALSE};
      } else if (types[i].equals(String.class)) {
        params[i] = new Object[] {"NOT_USED_STRING", "foo", "SECRET_ARRAY"};
      } else if (types[i].equals(Object.class)) {
        params[i] = new Object[] {null, "foo", "NOT_USED_STRING", Transform.class};
      } else if (types[i].isArray()) {
        params[i] = new Object[] {new Object[0], new Class[0], null};
      } else {
        throw new Exception("Unknown type " + types[i] + " at " + i + " in " + m);
      }
    }
    // Build the reset-iter.
    ResetIterator<Object[]> iter = new BaseResetIter();
    for (int i = params.length - 1; i >= 0; i--) {
      iter = new ComboIter(Arrays.asList(params[i]), iter);
    }
    final Iterator<Object[]> fiter = iter;
    // Wrap in an iterator with a useful toString method.
    return new Iterable<Object[]>() {
      public Iterator<Object[]> iterator() { return fiter; }
      public String toString() { return deepPrint(params); }
    };
  }

  public static String deepPrint(Object[] o) {
    return Arrays.toString(
        Arrays.stream(o)
            .map(
                (x) -> {
                  if (x == null) {
                    return "null";
                  } else if (x.getClass().isArray()) {
                    if (((Object[]) x).length == 0) {
                      return "new " + x.getClass().getComponentType().getName() + "[0]";
                    } else {
                      return deepPrint((Object[]) x);
                    }
                  } else {
                    return safePrint(x);
                  }
                })
            .toArray());
  }

  public static String safePrint(Object o) {
    if (o instanceof ClassLoader) {
      return o.getClass().getName();
    } else if (o == null) {
      return "null";
    } else if (o instanceof Exception) {
      String res = o.toString();
      if (res.endsWith("-transformed)")) {
        res = res.substring(0, res.lastIndexOf(" ")) + " <transformed-jar>)";
      } else if (res.endsWith(".jar)")) {
        res = res.substring(0, res.lastIndexOf(" ")) + " <original-jar>)";
      }
      return res;
    } else if (o instanceof Transform) {
      return "Transform Instance";
    } else if (o instanceof Class && isObsoleteObject((Class) o)) {
      return "(obsolete)" + o.toString();
    } else if (o.getClass().isArray()) {
      return Arrays.toString((Object[])o);
    } else {
      return o.toString();
    }
  }

  private static class ComboIter implements ResetIterator<Object[]> {
    private ResetIterator<Object[]> next;
    private Object cur;
    private boolean first;
    private Iterator<Object> my_vals;
    private Iterable<Object> my_vals_reset;

    public Object[] next() {
      if (!next.hasNext()) {
        cur = my_vals.next();
        first = false;
        if (next != null) {
          next.reset();
        }
      }
      if (first) {
        first = false;
        cur = my_vals.next();
      }
      Object[] nv = next.next();
      Object[] res = new Object[nv.length + 1];
      res[0] = cur;
      for (int i = 0; i < nv.length; i++) {
        res[i + 1] = nv[i];
      }
      return res;
    }

    public boolean hasNext() {
      return next.hasNext() || my_vals.hasNext();
    }

    public void reset() {
      my_vals = my_vals_reset.iterator();
      next.reset();
      cur = null;
      first = true;
    }

    public ComboIter(Iterable<Object> this_reset, ResetIterator<Object[]> next_reset) {
      my_vals_reset = this_reset;
      next = next_reset;
      reset();
    }
  }

  public static native long genericFieldOffset(Field f);

  public static native boolean isObsoleteObject(Class c);
}
