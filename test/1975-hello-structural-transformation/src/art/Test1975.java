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

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.ref.*;
import java.lang.reflect.*;
import java.util.*;

public class Test1975 {
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

  // Since we are adding fields we redefine this class with the Transform1975 class to add new
  // field-reads.
  public static final class ReadTransformFields implements Runnable {
    public void run() {
      System.out.println("Read CUR_CLASS field: " + printGeneric(Transform1975.CUR_CLASS));
      System.out.println(
          "Read REDEFINED_DEX_BYTES field: " + printGeneric(Transform1975.REDEFINED_DEX_BYTES));
    }
  }

  /* Base64 encoded dex file for:
   * public static final class ReadTransformFields implements Runnable {
   *   public void run() {
   *     System.out.println("Read CUR_CLASS field: " + printGeneric(Transform1975.CUR_CLASS));
   *     System.out.println("Read REDEFINED_DEX_BYTES field: " + printGeneric(Transform1975.REDEFINED_DEX_BYTES));
   *     System.out.println("Read NEW_STRING field: " + printGeneric(Transform1975.NEW_STRING));
   *   }
   * }
   */
  private static final byte[] NEW_READ_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQCHIfWvfkMos9E+Snhux5rSGhnDAbiVJlyYBgAAcAAAAHhWNBIAAAAAAAAAANQFAAAk"
                  + "AAAAcAAAAA4AAAAAAQAABQAAADgBAAAEAAAAdAEAAAgAAACUAQAAAQAAANQBAACkBAAA9AEAAO4C"
                  + "AAD2AgAAAQMAAAQDAAAIAwAALAMAADwDAABRAwAAdQMAAJUDAACsAwAAvwMAANMDAADpAwAA/QMA"
                  + "ABgEAAAsBAAAOAQAAE0EAABlBAAAfgQAAKAEAAC1BAAAxAQAAMcEAADLBAAAzwQAANwEAADkBAAA"
                  + "6gQAAO8EAAD9BAAABgUAAAsFAAAVBQAAHAUAAAQAAAAFAAAABgAAAAcAAAAIAAAACQAAAAoAAAAL"
                  + "AAAADAAAAA0AAAAOAAAADwAAABcAAAAZAAAAAgAAAAkAAAAAAAAAAwAAAAkAAADgAgAAAwAAAAoA"
                  + "AADoAgAAFwAAAAwAAAAAAAAAGAAAAAwAAADoAgAAAgAGAAEAAAACAAkAEAAAAAIADQARAAAACwAF"
                  + "AB0AAAAAAAMAAAAAAAAAAwAgAAAAAQABAB4AAAAFAAQAHwAAAAcAAwAAAAAACgADAAAAAAAKAAIA"
                  + "GwAAAAoAAAAhAAAAAAAAABEAAAAHAAAA2AIAABYAAADEBQAAowUAAAAAAAABAAEAAQAAAMYCAAAE"
                  + "AAAAcBAEAAAADgAFAAEAAgAAAMoCAABVAAAAYgADAGIBAABxEAIAAQAMASICCgBwEAUAAgAaAxIA"
                  + "biAGADIAbiAGABIAbhAHAAIADAFuIAMAEABiAAMAYgECAHEQAgABAAwBIgIKAHAQBQACABoDFABu"
                  + "IAYAMgBuIAYAEgBuEAcAAgAMAW4gAwAQAGIAAwBiAQEAcRACAAEADAEiAgoAcBAFAAIAGgMTAG4g"
                  + "BgAyAG4gBgASAG4QBwACAAwBbiADABAADgAEAA4ABgAOARwPARwPARwPAAABAAAACAAAAAEAAAAH"
                  + "AAAAAQAAAAkABjxpbml0PgAJQ1VSX0NMQVNTAAFMAAJMTAAiTGFydC9UZXN0MTk3NSRSZWFkVHJh"
                  + "bnNmb3JtRmllbGRzOwAOTGFydC9UZXN0MTk3NTsAE0xhcnQvVHJhbnNmb3JtMTk3NTsAIkxkYWx2"
                  + "aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNs"
                  + "YXNzOwAVTGphdmEvaW8vUHJpbnRTdHJlYW07ABFMamF2YS9sYW5nL0NsYXNzOwASTGphdmEvbGFu"
                  + "Zy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2"
                  + "YS9sYW5nL1N0cmluZ0J1aWxkZXI7ABJMamF2YS9sYW5nL1N5c3RlbTsACk5FV19TVFJJTkcAE1JF"
                  + "REVGSU5FRF9ERVhfQllURVMAFlJlYWQgQ1VSX0NMQVNTIGZpZWxkOiAAF1JlYWQgTkVXX1NUUklO"
                  + "RyBmaWVsZDogACBSZWFkIFJFREVGSU5FRF9ERVhfQllURVMgZmllbGQ6IAATUmVhZFRyYW5zZm9y"
                  + "bUZpZWxkcwANVGVzdDE5NzUuamF2YQABVgACVkwAAltCAAthY2Nlc3NGbGFncwAGYXBwZW5kAARu"
                  + "YW1lAANvdXQADHByaW50R2VuZXJpYwAHcHJpbnRsbgADcnVuAAh0b1N0cmluZwAFdmFsdWUAdn5+"
                  + "RDh7ImNvbXBpbGF0aW9uLW1vZGUiOiJkZWJ1ZyIsIm1pbi1hcGkiOjEsInNoYS0xIjoiYTgzNTJm"
                  + "MjU0ODg1MzYyY2NkOGQ5MDlkMzUyOWM2MDA5NGRkODk2ZSIsInZlcnNpb24iOiIxLjYuMjAtZGV2"
                  + "In0AAgMBIhgBAgQCGgQZHBcVAAABAQCBgAT0AwEBjAQAAAAAAAAAAgAAAJQFAACaBQAAuAUAAAAA"
                  + "AAAAAAAAAAAAABAAAAAAAAAAAQAAAAAAAAABAAAAJAAAAHAAAAACAAAADgAAAAABAAADAAAABQAA"
                  + "ADgBAAAEAAAABAAAAHQBAAAFAAAACAAAAJQBAAAGAAAAAQAAANQBAAABIAAAAgAAAPQBAAADIAAA"
                  + "AgAAAMYCAAABEAAAAwAAANgCAAACIAAAJAAAAO4CAAAEIAAAAgAAAJQFAAAAIAAAAQAAAKMFAAAD"
                  + "EAAAAgAAALQFAAAGIAAAAQAAAMQFAAAAEAAAAQAAANQFAAA=");

  static void ReadFields() throws Exception {
    Runnable r = new ReadTransformFields();
    System.out.println("Reading with reflection.");
    for (Field f : Transform1975.class.getFields()) {
      System.out.println(f.toString() + " = " + printGeneric(f.get(null)));
    }
    System.out.println("Reading normally in same class.");
    Transform1975.readFields();
    System.out.println("Reading with native.");
    readNativeFields(Transform1975.class, getNativeFields(Transform1975.class.getFields()));
    System.out.println("Reading normally in other class.");
    r.run();
    System.out.println("Reading using method handles.");
    readMethodHandles(getMethodHandles(Transform1975.class.getFields()));
    System.out.println("Doing modification maybe");
    Transform1975.doSomething();
    System.out.println("Reading with reflection after possible modification.");
    for (Field f : Transform1975.class.getFields()) {
      System.out.println(f.toString() + " = " + printGeneric(f.get(null)));
    }
    System.out.println("Reading normally in same class after possible modification.");
    Transform1975.readFields();
    System.out.println("Reading with native after possible modification.");
    readNativeFields(Transform1975.class, getNativeFields(Transform1975.class.getFields()));
    System.out.println("Reading normally in other class after possible modification.");
    r.run();
    System.out.println("Reading using method handles.");
    readMethodHandles(getMethodHandles(Transform1975.class.getFields()));
  }

  public static final class MethodHandleWrapper {
    private MethodHandle mh;
    private Field f;
    public MethodHandleWrapper(MethodHandle mh, Field f) {
      this.f = f;
      this.mh = mh;
    }
    public MethodHandle getHandle() {
      return mh;
    }
    public Field getField() {
      return f;
    }
    public Object invoke() throws Throwable {
      return mh.invoke();
    }
    public String toString() {
      return mh.toString();
    }
  }

  public static MethodHandleWrapper[] getMethodHandles(Field[] fields) throws Exception {
    final MethodHandles.Lookup l = MethodHandles.lookup();
    MethodHandleWrapper[] res = new MethodHandleWrapper[fields.length];
    for (int i = 0; i < res.length; i++) {
      res[i] = new MethodHandleWrapper(l.unreflectGetter(fields[i]), fields[i]);;
    }
    return res;
  }

  public static void readMethodHandles(MethodHandleWrapper[] handles) throws Exception {
    for (MethodHandleWrapper h : handles) {
      try {
        System.out.println(printGeneric(h) + " (" + h.getField() + ") = " + printGeneric(h.invoke()));
      } catch (Throwable t) {
        if (t instanceof Exception) {
          throw (Exception)t;
        } else if (t instanceof Error) {
          throw (Error)t;
        } else {
          throw new RuntimeException("Unexpected throwable thrown!", t);
        }
      }
    }
  }
  public static void doTest() throws Exception {
    // TODO It would be good to have a test of invoke-custom too but since that requires smali and
    // internally we just store the resolved MethodHandle this should all be good enough.

    // Grab Field objects from before the transformation.
    Field[] old_fields = Transform1975.class.getFields();
    for (Field f : old_fields) {
      System.out.println("Saving Field object " + printGeneric(f) + " for later");
    }
    // Grab jfieldIDs from before the transformation.
    long[] old_native_fields = getNativeFields(Transform1975.class.getFields());
    // Grab MethodHandles from before the transformation.
    MethodHandleWrapper[] handles = getMethodHandles(Transform1975.class.getFields());
    for (MethodHandleWrapper h : handles) {
      System.out.println("Saving MethodHandle object " + printGeneric(h) + " for later");
    }
    // Grab a 'setter' MethodHandle from before the redefinition.
    Field cur_class_field = Transform1975.class.getDeclaredField("CUR_CLASS");
    MethodHandleWrapper write_wrapper = new MethodHandleWrapper(MethodHandles.lookup().unreflectSetter(cur_class_field), cur_class_field);
    System.out.println("Saving writable MethodHandle " + printGeneric(write_wrapper) + " for later");

    // Read the fields in all possible ways.
    System.out.println("Reading fields before redefinition");
    ReadFields();
    // Redefine the transform class. Also change the ReadTransformFields so we don't have to deal
    // with annoying compilation stuff.
    Redefinition.doCommonStructuralClassRedefinition(
        Transform1975.class, Transform1975.REDEFINED_DEX_BYTES);
    Redefinition.doCommonClassRedefinition(
        ReadTransformFields.class, new byte[] {}, NEW_READ_BYTES);
    // Read the fields in all possible ways.
    System.out.println("Reading fields after redefinition");
    ReadFields();
    // Check that the old Field, jfieldID, and MethodHandle objects were updated.
    System.out.println("reading reflectively with old reflection objects");
    for (Field f : old_fields) {
      System.out.println("OLD FIELD OBJECT: " + f.toString() + " = " + printGeneric(f.get(null)));
    }
    System.out.println("reading natively with old jfieldIDs");
    readNativeFields(Transform1975.class, old_native_fields);
    // Make sure the fields keep the same id.
    System.out.println("reading natively with new jfieldIDs");
    long[] new_fields = getNativeFields(Transform1975.class.getFields());
    Arrays.sort(old_native_fields);
    Arrays.sort(new_fields);
    boolean different = new_fields.length == old_native_fields.length;
    for (int i = 0; i < old_native_fields.length && !different; i++) {
      different = different || new_fields[i] != old_native_fields[i];
    }
    if (different) {
      System.out.println(
          "Missing expected fields! "
              + Arrays.toString(new_fields)
              + " vs "
              + Arrays.toString(old_native_fields));
    }
    // Make sure the old handles work.
    System.out.println("Reading with old method handles");
    readMethodHandles(handles);
    System.out.println("Reading with new method handles");
    readMethodHandles(getMethodHandles(Transform1975.class.getFields()));
    System.out.println("Writing " + printGeneric(Test1975.class) + " to CUR_CLASS with old method handle");
    try {
      write_wrapper.getHandle().invokeExact(Test1975.class);
    } catch (Throwable t) {
      throw new RuntimeException("something threw", t);
    }
    System.out.println("Reading changed value");
    System.out.println("CUR_CLASS is now " + printGeneric(Transform1975.CUR_CLASS));
  }

  private static void printNativeField(long id, Field f, Object value) {
    System.out.println(
        "Field" + (PRINT_NONDETERMINISTIC ? " " + id : "") + " " + f + " = " + printGeneric(value));
  }

  public static native long[] getNativeFields(Field[] fields);

  public static native void readNativeFields(Class<?> field_class, long[] sfields);
}
