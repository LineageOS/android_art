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

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.concurrent.CountDownLatch;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;

public class Test1974 {

  public static final boolean DEBUG = false;

  public static int[] static_field = new int[] {1, 2, 3};
  public static Object[] static_field_ref = new String[] {"a", "b", "c"};

  public static final class InstanceClass {
    public int[] instance_field = new int[] {1, 2, 3};
    public Object[] self_ref;

    public InstanceClass() {
      self_ref = new Object[] {null, "A", "B", "C"};
      self_ref[0] = self_ref;
    }
  }

  static InstanceClass theInstanceClass;
  static InstanceClass theOtherInstanceClass;

  static {
    theInstanceClass = new InstanceClass();
    theOtherInstanceClass = new InstanceClass();
    theOtherInstanceClass.instance_field = theInstanceClass.instance_field;
    theOtherInstanceClass.self_ref = theInstanceClass.self_ref;
  }

  public static void DbgPrintln(String s) {
    if (DEBUG) {
      System.out.println(s);
    }
  }

  public interface ThrowRunnable extends Runnable {
    public default void run() {
      try {
        throwRun();
      } catch (Exception e) {
        throw new Error("Exception in runner!", e);
      }
    }

    public void throwRun() throws Exception;
  }

  public static void runAsThread(ThrowRunnable r) throws Exception {
    Thread t = new Thread(r);
    t.start();
    t.join();
    System.out.println("");
  }

  public static void runInstance() {
    System.out.println("Test instance");
    DbgPrintln("Pre hash: " + theInstanceClass.instance_field.hashCode());
    System.out.println(
        "val is: " + Arrays.toString(theInstanceClass.instance_field) + " resize +3");
    ResizeArray(() -> theInstanceClass.instance_field, theInstanceClass.instance_field.length + 5);
    System.out.println("val is: " + Arrays.toString(theInstanceClass.instance_field));
    DbgPrintln("Post hash: " + theInstanceClass.instance_field.hashCode());
    System.out.println(
        "Same value? " + (theInstanceClass.instance_field == theOtherInstanceClass.instance_field));
  }

  public static void runHashMap() {
    System.out.println("Test HashMap");
    HashMap<byte[], Comparable> map = new HashMap();
    Comparable the_value = "THE VALUE";
    Supplier<byte[]> get_the_value =
        () ->
            map.entrySet().stream()
                .filter((x) -> x.getValue().equals(the_value))
                .findFirst()
                .get()
                .getKey();
    map.put(new byte[] {1, 2, 3, 4}, the_value);
    map.put(new byte[] {1, 2, 3, 4}, "Other Value");
    map.put(new byte[] {1, 4}, "Third value");
    System.out.println("val is: " + Arrays.toString(get_the_value.get()) + " resize +3");
    System.out.print("Map is: ");
    map.entrySet().stream()
        .sorted((x, y) -> x.getValue().compareTo(y.getValue()))
        .forEach(
            (e) -> {
              System.out.print("(" + Arrays.toString(e.getKey()) + "->" + e.getValue() + "), ");
            });
    System.out.println();
    ResizeArray(get_the_value, 7);
    System.out.println("val is: " + Arrays.toString(get_the_value.get()));
    System.out.print("Map is: ");
    map.entrySet().stream()
        .sorted((x, y) -> x.getValue().compareTo(y.getValue()))
        .forEach(
            (e) -> {
              System.out.print("(" + Arrays.toString(e.getKey()) + "->" + e.getValue() + "), ");
            });
    System.out.println();
  }

  public static void runWeakReference() {
    System.out.println("Test j.l.r.WeakReference");
    String[] arr = new String[] {"weak", "ref"};
    WeakReference<String[]> wr = new WeakReference(arr);
    DbgPrintln("Pre hash: " + wr.get().hashCode());
    System.out.println("val is: " + Arrays.toString(wr.get()) + " resize +3");
    ResizeArray(wr::get, wr.get().length + 5);
    System.out.println("val is: " + Arrays.toString(wr.get()));
    DbgPrintln("Post hash: " + wr.get().hashCode());
    System.out.println("Same value? " + (wr.get() == arr));
  }

  public static void runInstanceSelfRef() {
    System.out.println("Test instance self-ref");
    DbgPrintln("Pre hash: " + Integer.toHexString(theInstanceClass.self_ref.hashCode()));
    String pre_to_string = theInstanceClass.self_ref.toString();
    System.out.println(
        "val is: "
            + Arrays.toString(theInstanceClass.self_ref).replace(pre_to_string, "<SELF REF>")
            + " resize +5 item 0 is "
            + Arrays.toString((Object[]) theInstanceClass.self_ref[0])
                .replace(pre_to_string, "<SELF REF>"));
    ResizeArray(() -> theInstanceClass.self_ref, theInstanceClass.self_ref.length + 5);
    System.out.println(
        "val is: "
            + Arrays.toString(theInstanceClass.self_ref).replace(pre_to_string, "<SELF REF>"));
    System.out.println(
        "val is: "
            + Arrays.toString((Object[]) theInstanceClass.self_ref[0])
                .replace(pre_to_string, "<SELF REF>"));
    DbgPrintln("Post hash: " + Integer.toHexString(theInstanceClass.self_ref.hashCode()));
    System.out.println(
        "Same value? " + (theInstanceClass.self_ref == theOtherInstanceClass.self_ref));
    System.out.println(
        "Same structure? " + (theInstanceClass.self_ref == theInstanceClass.self_ref[0]));
    System.out.println(
        "Same inner-structure? "
            + (theInstanceClass.self_ref[0] == ((Object[]) theInstanceClass.self_ref[0])[0]));
  }

  public static void runInstanceSelfRefSmall() {
    System.out.println("Test instance self-ref smaller");
    DbgPrintln("Pre hash: " + Integer.toHexString(theInstanceClass.self_ref.hashCode()));
    String pre_to_string = theInstanceClass.self_ref.toString();
    System.out.println(
        "val is: "
            + Arrays.toString(theInstanceClass.self_ref).replace(pre_to_string, "<SELF REF>")
            + " resize -7 item 0 is "
            + Arrays.toString((Object[]) theInstanceClass.self_ref[0])
                .replace(pre_to_string, "<SELF REF>"));
    ResizeArray(() -> theInstanceClass.self_ref, theInstanceClass.self_ref.length - 7);
    System.out.println(
        "val is: "
            + Arrays.toString(theInstanceClass.self_ref).replace(pre_to_string, "<SELF REF>"));
    System.out.println(
        "val is: "
            + Arrays.toString((Object[]) theInstanceClass.self_ref[0])
                .replace(pre_to_string, "<SELF REF>"));
    DbgPrintln("Post hash: " + Integer.toHexString(theInstanceClass.self_ref.hashCode()));
    System.out.println(
        "Same value? " + (theInstanceClass.self_ref == theOtherInstanceClass.self_ref));
    System.out.println(
        "Same structure? " + (theInstanceClass.self_ref == theInstanceClass.self_ref[0]));
    System.out.println(
        "Same inner-structure? "
            + (theInstanceClass.self_ref[0] == ((Object[]) theInstanceClass.self_ref[0])[0]));
  }

  public static void runLocal() throws Exception {
    final int[] arr_loc = new int[] {2, 3, 4};
    int[] arr_loc_2 = arr_loc;

    System.out.println("Test local");
    DbgPrintln("Pre hash: " + arr_loc.hashCode());
    System.out.println("val is: " + Arrays.toString(arr_loc) + " resize +5");
    ResizeArray(() -> arr_loc, arr_loc.length + 5);
    System.out.println("val is: " + Arrays.toString(arr_loc));
    DbgPrintln("Post hash: " + arr_loc.hashCode());
    System.out.println("Same value? " + (arr_loc == arr_loc_2));
  }

  public static void runLocalSmall() throws Exception {
    final int[] arr_loc = new int[] {1, 2, 3, 4, 5};
    int[] arr_loc_2 = arr_loc;

    System.out.println("Test local smaller");
    DbgPrintln("Pre hash: " + arr_loc.hashCode());
    System.out.println("val is: " + Arrays.toString(arr_loc) + " resize -2");
    ResizeArray(() -> arr_loc, arr_loc.length - 2);
    System.out.println("val is: " + Arrays.toString(arr_loc));
    DbgPrintln("Post hash: " + arr_loc.hashCode());
    System.out.println("Same value? " + (arr_loc == arr_loc_2));
  }

  public static void runMultiThreadLocal() throws Exception {
    final CountDownLatch cdl = new CountDownLatch(1);
    final CountDownLatch start_cdl = new CountDownLatch(2);
    final Supplier<Object[]> getArr =
        new Supplier<Object[]>() {
          public final Object[] arr = new Object[] {"1", "2", "3"};

          public Object[] get() {
            return arr;
          }
        };
    final ArrayList<String> msg1 = new ArrayList();
    final ArrayList<String> msg2 = new ArrayList();
    final Consumer<String> print1 =
        (String s) -> {
          msg1.add(s);
        };
    final Consumer<String> print2 =
        (String s) -> {
          msg2.add(s);
        };
    Function<Consumer<String>, Runnable> r =
        (final Consumer<String> c) ->
            () -> {
              c.accept("Test local multi-thread");
              Object[] arr_loc = getArr.get();
              Object[] arr_loc_2 = getArr.get();

              DbgPrintln("Pre hash: " + arr_loc.hashCode());
              c.accept("val is: " + Arrays.toString(arr_loc) + " resize -2");

              try {
                start_cdl.countDown();
                cdl.await();
              } catch (Exception e) {
                throw new Error("failed await", e);
              }
              c.accept("val is: " + Arrays.toString(arr_loc));
              DbgPrintln("Post hash: " + arr_loc.hashCode());
              c.accept("Same value? " + (arr_loc == arr_loc_2));
            };
    Thread t1 = new Thread(r.apply(print1));
    Thread t2 = new Thread(r.apply(print2));
    t1.start();
    t2.start();
    start_cdl.await();
    ResizeArray(getArr, 1);
    cdl.countDown();
    t1.join();
    t2.join();
    for (String s : msg1) {
      System.out.println("T1: " + s);
    }
    for (String s : msg2) {
      System.out.println("T2: " + s);
    }
  }

  public static void runWithLocks() throws Exception {
    final CountDownLatch cdl = new CountDownLatch(1);
    final CountDownLatch start_cdl = new CountDownLatch(2);
    final CountDownLatch waiter_start_cdl = new CountDownLatch(1);
    final Supplier<Object[]> getArr =
        new Supplier<Object[]>() {
          public final Object[] arr = new Object[] {"A", "2", "C"};

          public Object[] get() {
            return arr;
          }
        };
    // basic order of operations noted above each line.
    // Waiter runs to the 'wait' then t1 runs to the cdl.await, then current thread runs.
    Runnable r =
        () -> {
          System.out.println("Test locks");
          Object[] arr_loc = getArr.get();
          Object[] arr_loc_2 = getArr.get();

          DbgPrintln("Pre hash: " + arr_loc.hashCode());
          System.out.println("val is: " + Arrays.toString(arr_loc) + " resize -2");

          try {
            // OP 1
            waiter_start_cdl.await();
            // OP 6
            synchronized (arr_loc) {
              // OP 7
              synchronized (arr_loc_2) {
                // OP 8
                start_cdl.countDown();
                // OP 9
                cdl.await();
                // OP 13
              }
            }
          } catch (Exception e) {
            throw new Error("failed await", e);
          }
          System.out.println("val is: " + Arrays.toString(arr_loc));
          DbgPrintln("Post hash: " + arr_loc.hashCode());
          System.out.println("Same value? " + (arr_loc == arr_loc_2));
        };
    Thread t1 = new Thread(r);
    Thread waiter =
        new Thread(
            () -> {
              try {
                Object a = getArr.get();
                // OP 2
                synchronized (a) {
                  // OP 3
                  waiter_start_cdl.countDown();
                  // OP 4
                  start_cdl.countDown();
                  // OP 5
                  a.wait();
                  // OP 15
                }
              } catch (Exception e) {
                throw new Error("Failed wait!", e);
              }
            });
    waiter.start();
    t1.start();
    // OP 10
    start_cdl.await();
    // OP 11
    ResizeArray(getArr, 1);
    // OP 12
    cdl.countDown();
    // OP 14
    synchronized (getArr.get()) {
      // Make sure thread wakes up and has the right lock.
      getArr.get().notifyAll();
    }
    waiter.join();
    t1.join();
    // Make sure other threads can still lock it.
    synchronized (getArr.get()) {
    }
    System.out.println("Locks seem to all work.");
  }

  public static void runWithJniGlobal() throws Exception {
    Object[] arr = new Object[] {"1", "11", "111"};
    final long globalID = GetGlobalJniRef(arr);
    System.out.println("Test jni-ref");
    DbgPrintln("Pre hash: " + ReadJniRef(globalID).hashCode());
    System.out.println(
        "val is: " + Arrays.toString((Object[]) ReadJniRef(globalID)) + " resize +5");
    ResizeArray(() -> ReadJniRef(globalID), ((Object[]) ReadJniRef(globalID)).length + 5);
    System.out.println("val is: " + Arrays.toString((Object[]) ReadJniRef(globalID)));
    DbgPrintln("Post hash: " + ReadJniRef(globalID).hashCode());
    System.out.println("Same value? " + (ReadJniRef(globalID) == arr));
  }

  public static void runWithJniWeakGlobal() throws Exception {
    Object[] arr = new Object[] {"2", "22", "222"};
    final long globalID = GetWeakGlobalJniRef(arr);
    System.out.println("Test weak jni-ref");
    DbgPrintln("Pre hash: " + ReadJniRef(globalID).hashCode());
    System.out.println(
        "val is: " + Arrays.toString((Object[]) ReadJniRef(globalID)) + " resize +5");
    ResizeArray(() -> ReadJniRef(globalID), ((Object[]) ReadJniRef(globalID)).length + 5);
    System.out.println("val is: " + Arrays.toString((Object[]) ReadJniRef(globalID)));
    DbgPrintln("Post hash: " + ReadJniRef(globalID).hashCode());
    System.out.println("Same value? " + (ReadJniRef(globalID) == arr));
    if (ReadJniRef(globalID) != arr) {
      throw new Error("Didn't update weak global!");
    }
  }

  public static void runWithJniLocals() throws Exception {
    final Object[] arr = new Object[] {"3", "32", "322"};
    System.out.println("Test jni local ref");
    Consumer<Object> checker = (o) -> System.out.println("Same value? " + (o == arr));
    Consumer<Object> printer =
        (o) -> System.out.println("val is: " + Arrays.toString((Object[]) o));
    Runnable resize =
        () -> {
          System.out.println("Resize +4");
          ResizeArray(() -> arr, arr.length + 4);
        };
    runNativeTest(arr, resize, printer, checker);
  }

  public static native void runNativeTest(
      Object[] arr, Runnable resize, Consumer<Object> printer, Consumer<Object> checker);

  public static void runWithJvmtiTags() throws Exception {
    Object[] arr = new Object[] {"3", "33", "333"};
    long globalID = 333_333_333l;
    Main.setTag(arr, globalID);
    System.out.println("Test jvmti-tags");
    DbgPrintln("Pre hash: " + arr.hashCode());
    System.out.println(
        "val is: " + Arrays.deepToString(GetObjectsWithTag(globalID)) + " resize +5");
    ResizeArray(() -> arr, arr.length + 5);
    Object[] after_tagged_obj = GetObjectsWithTag(globalID);
    System.out.println("val is: " + Arrays.deepToString(GetObjectsWithTag(globalID)));
    DbgPrintln("Post hash: " + after_tagged_obj[0].hashCode());
    System.out.println("Same value? " + (after_tagged_obj[0] == arr));
  }

  public static void runWithJvmtiTagsObsolete() throws Exception {
    Object[] arr = new Object[] {"4", "44", "444"};
    long globalID = 444_444_444l;
    System.out.println("Test jvmti-tags with obsolete");
    Main.setTag(arr, globalID);
    StartCollectFrees();
    StartAssignObsoleteIncrementedId();
    DbgPrintln("Pre hash: " + arr.hashCode());
    System.out.println(
        "val is: " + Arrays.deepToString(GetObjectsWithTag(globalID)) + " resize +5");
    ResizeArray(() -> arr, arr.length + 5);
    Object[] after_tagged_obj = GetObjectsWithTag(globalID);
    Object[] obsolete_tagged_obj = GetObjectsWithTag(globalID + 1);
    System.out.println("val is: " + Arrays.deepToString(GetObjectsWithTag(globalID)));
    EndAssignObsoleteIncrementedId();
    long[] obsoletes_freed = CollectFreedTags();
    DbgPrintln("Post hash: " + after_tagged_obj[0].hashCode());
    System.out.println("Same value? " + (after_tagged_obj[0] == arr));
    if (obsolete_tagged_obj.length >= 1) {
      DbgPrintln("Found objects with obsolete tag: " + Arrays.deepToString(obsolete_tagged_obj));
      boolean bad = false;
      if (obsolete_tagged_obj.length != 1) {
        System.out.println(
            "Found obsolete tag'd objects: "
                + Arrays.deepHashCode(obsolete_tagged_obj)
                + " but only expected one!");
        bad = true;
      }
      if (!Arrays.deepEquals(
          Arrays.copyOf(arr, ((Object[]) obsolete_tagged_obj[0]).length),
          (Object[]) obsolete_tagged_obj[0])) {
        System.out.println("Obsolete array was unexpectedly different than non-obsolete one!");
        bad = true;
      }
      if (!Arrays.stream(obsoletes_freed).anyMatch((l) -> l == globalID + 1)) {
        DbgPrintln("Didn't see a free of the obsolete id");
      }
      if (!bad) {
        System.out.println("Everything looks good WRT obsolete object!");
      }
    } else {
      if (!Arrays.stream(obsoletes_freed).anyMatch((l) -> l == globalID + 1)) {
        System.out.println("Didn't see a free of the obsolete id");
      } else {
        DbgPrintln("Saw a free of obsolete id!");
        System.out.println("Everything looks good WRT obsolete object!");
      }
    }
  }

  public static void run() throws Exception {
    // Simple
    runAsThread(Test1974::runInstance);

    // HashMap
    runAsThread(Test1974::runHashMap);

    // j.l.ref.WeakReference
    runAsThread(Test1974::runWeakReference);

    // Self-referential arrays.
    runAsThread(Test1974::runInstanceSelfRef);
    runAsThread(Test1974::runInstanceSelfRefSmall);

    // Local variables simple
    runAsThread(Test1974::runLocal);
    runAsThread(Test1974::runLocalSmall);

    // multiple threads local variables
    runAsThread(Test1974::runMultiThreadLocal);

    // using as monitors and waiting
    runAsThread(Test1974::runWithLocks);

    // Basic jni global refs
    runAsThread(Test1974::runWithJniGlobal);

    // Basic jni weak global refs
    runAsThread(Test1974::runWithJniWeakGlobal);

    // Basic JNI local refs
    runAsThread(Test1974::runWithJniLocals);

    // Basic jvmti tags
    runAsThread(Test1974::runWithJvmtiTags);

    // Grab obsolete reference using tags/detect free
    runAsThread(Test1974::runWithJvmtiTagsObsolete);
  }

  // Use a supplier so that we don't have to have a local ref to the resized
  // array if we don't want it
  public static native <T> void ResizeArray(Supplier<T> arr, int new_size);

  public static native <T> long GetGlobalJniRef(T t);

  public static native <T> long GetWeakGlobalJniRef(T t);

  public static native <T> T ReadJniRef(long t);

  public static native Object[] GetObjectsWithTag(long tag);

  public static native void StartCollectFrees();

  public static native void StartAssignObsoleteIncrementedId();

  public static native void EndAssignObsoleteIncrementedId();

  public static native long[] CollectFreedTags();
}
