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

package art;

import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Set;
import java.util.concurrent.Semaphore;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.IntConsumer;
import java.util.function.Predicate;
import java.util.function.Supplier;

public class Test1966 {
  public static final String TARGET_VAR = "TARGET";

  public static interface TestInterface {
    public default void doNothing() {}
  }
  public static class TestClass1 implements TestInterface {
    public String id;
    public TestClass1(String id) {
      this.id = id;
    }
    public String toString() {
      return String.format("TestClass1(\"%s\")", id);
    }

    public static TestInterface createInterface(String id) {
      return new TestClass1(id);
    }
    public static TestClass1 createExact(String id) {
      return new TestClass1(id);
    }
    public static Object create(String id) {
      return new TestClass1(id);
    }
  }

  public static class TestClass1ext extends TestClass1 {
    public TestClass1ext(String id) {
      super(id);
    }
    public String toString() {
      return String.format("TestClass1ext(\"%s\")", super.toString());
    }
  }
  public static class TestClass2 {
    public String id;
    public TestClass2(String id) {
      this.id = id;
    }
    public String toString() {
      return String.format("TestClass2(\"%s\")", id);
    }
  }
  public static class TestClass2impl extends TestClass2 implements TestInterface {
    public TestClass2impl(String id) {
      super(id);
    }
    public String toString() {
      return String.format("TestClass2impl(\"%s\")", super.toString());
    }
  }

  public static void reportValue(Object val) {
    System.out.println("\tValue is '" + val +
                       "' (class: " + (val != null ? val.getClass() : "NULL") + ")");
  }

  public static interface SafepointFunction {
    public void invoke(Thread thread, Method target, int slot, int depth) throws Exception;
  }

  public static interface SetterFunction {
    public void SetVar(Thread t, int depth, int slot, Object v);
  }

  public static interface GetterFunction { public Object GetVar(Thread t, int depth, int slot); }

  public static SafepointFunction
  NamedSet(final String type, final SetterFunction get, final Object v) {
    return new SafepointFunction() {
      public void invoke(Thread t, Method method, int slot, int depth) {
        try {
          get.SetVar(t, depth, slot, v);
          System.out.println(this + " on " + method + " set value: " + v);
        } catch (Exception e) {
          System.out.println(this + " on " + method + " failed to set value " + v + " due to " +
                             e.getMessage());
        }
      }
      public String toString() {
        return "\"Set" + type + "\"";
      }
    };
  }

  public static SafepointFunction NamedGet(final String type, final GetterFunction get) {
    return new SafepointFunction() {
      public void invoke(Thread t, Method method, int slot, int depth) {
        try {
          Object res = get.GetVar(t, depth, slot);
          System.out.println(this + " on " + method + " got value: " + res);
        } catch (Exception e) {
          System.out.println(this + " on " + method + " failed due to " + e.getMessage());
        }
      }
      public String toString() {
        return "\"Get" + type + "\"";
      }
    };
  }

  public static class TestCase {
    public final Method target;

    public TestCase(Method target) {
      this.target = target;
    }

    public static class ThreadPauser implements IntConsumer {
      public final Semaphore sem_wakeup_main;
      public final Semaphore sem_wait;
      public int slot = -1;

      public ThreadPauser() {
        sem_wakeup_main = new Semaphore(0);
        sem_wait = new Semaphore(0);
      }

      public void accept(int i) {
        try {
          slot = i;
          sem_wakeup_main.release();
          sem_wait.acquire();
        } catch (Exception e) {
          throw new Error("Error with semaphores!", e);
        }
      }

      public void waitForOtherThreadToPause() throws Exception {
        sem_wakeup_main.acquire();
      }

      public void wakeupOtherThread() throws Exception {
        sem_wait.release();
      }
    }

    public void exec(final SafepointFunction safepoint) throws Exception {
      System.out.println("Running " + target + " with " + safepoint + " on remote thread.");
      final ThreadPauser pause = new ThreadPauser();
      Thread remote = new Thread(() -> {
        try {
          target.invoke(null, pause);
        } catch (Exception e) {
          throw new Error("Error invoking remote thread " + Thread.currentThread(), e);
        }
      }, "remote thread for " + target + " with " + safepoint);
      remote.start();
      pause.waitForOtherThreadToPause();
      try {
        Suspension.suspend(remote);
        StackTrace.StackFrameData frame = findStackFrame(remote);
        safepoint.invoke(remote, target, pause.slot, frame.depth);
      } finally {
        Suspension.resume(remote);
        pause.wakeupOtherThread();
        remote.join();
      }
    }

    private StackTrace.StackFrameData findStackFrame(Thread thr) {
      for (StackTrace.StackFrameData frame : StackTrace.GetStackTrace(thr)) {
        if (frame.method.equals(target)) {
          return frame;
        }
      }
      throw new Error("Unable to find stack frame in method " + target + " on thread " + thr);
    }
  }
  public static Method getMethod(String name) throws Exception {
    return Class.forName("art_test.TestCases1966").getDeclaredMethod(name, IntConsumer.class);
  }

  public static void run() throws Exception {
    Locals.EnableLocalVariableAccess();
    final TestCase[] MAIN_TEST_CASES = new TestCase[] {
      new TestCase(getMethod("ObjectMethod")),
      new TestCase(getMethod("CastInterfaceMethod")),
      new TestCase(getMethod("CastExactMethod")),
      new TestCase(getMethod("InterfaceMethod")),
      new TestCase(getMethod("ExactClassMethod")),
      new TestCase(getMethod("PrimitiveMethod")),
      new TestCase(getMethod("NullMethod")),
      new TestCase(getMethod("CastExactNullMethod")),
      new TestCase(getMethod("CastInterfaceNullMethod")),
    };

    final SetterFunction set_obj = Locals::SetLocalVariableObject;
    final SafepointFunction[] SAFEPOINTS = new SafepointFunction[] {
      NamedGet("GetObject", Locals::GetLocalVariableObject),
      NamedSet("Null", set_obj, null),
      NamedSet("TestClass1", set_obj, new TestClass1("Set TestClass1")),
      NamedSet("TestClass1ext", set_obj, new TestClass1ext("Set TestClass1ext")),
      NamedSet("TestClass2", set_obj, new TestClass2("Set TestClass2")),
      NamedSet("TestClass2impl", set_obj, new TestClass2impl("Set TestClass2impl")),
    };

    for (TestCase t : MAIN_TEST_CASES) {
      for (SafepointFunction s : SAFEPOINTS) {
        t.exec(s);
      }
    }
  }
}
