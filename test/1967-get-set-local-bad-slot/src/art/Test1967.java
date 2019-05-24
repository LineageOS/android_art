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

import java.lang.reflect.Method;
import java.util.concurrent.Semaphore;
import java.util.function.ToIntFunction;

public class Test1967 {
  public static final String TARGET_VAR = "TARGET";

  public static void reportValue(Object val) {
    if (val instanceof Character) {
      val = "<Char: " + Character.getNumericValue(((Character) val).charValue()) + ">";
    }
    System.out.println(
        "\tValue is '"
            + val
            + "' (class: "
            + (val != null ? val.getClass().toString() : "null")
            + ")");
  }

  public static void ObjectMethod(Runnable safepoint) {
    Object TARGET = "TARGET OBJECT";
    safepoint.run();
    reportValue(TARGET);
  }

  public static void IntMethod(Runnable safepoint) {
    int TARGET = 42;
    safepoint.run();
    reportValue(TARGET);
  }

  public static void LongMethod(Runnable safepoint) {
    long TARGET = 9001;
    safepoint.run();
    reportValue(TARGET);
  }

  public static interface SafepointFunction {
    public void invoke(
        Thread thread, Method target, Locals.VariableDescription TARGET_desc, int depth)
        throws Exception;
  }

  public static interface SetterFunction {
    public void SetVar(Thread t, int depth, int slot, Object v);
  }

  public static interface GetterFunction {
    public Object GetVar(Thread t, int depth, int slot);
  }

  public static SafepointFunction BadSet(
      final String type,
      final SetterFunction get,
      final Object v,
      final ToIntFunction<Integer> transform) {
    return new SafepointFunction() {
      public void invoke(Thread t, Method method, Locals.VariableDescription desc, int depth) {
        int real_slot = transform.applyAsInt(desc.slot);
        try {
          get.SetVar(t, depth, real_slot, v);
          System.out.println(this + " on " + method + " set value: " + v);
        } catch (Exception e) {
          System.out.println(
              this + " on " + method + " failed to set value " + v + " due to " + e.getMessage());
        }
      }

      public String toString() {
        return "\"Set" + type + "\"";
      }
    };
  }

  public static SafepointFunction BadGet(
      final String type, final GetterFunction get, final ToIntFunction<Integer> transform) {
    return new SafepointFunction() {
      public void invoke(Thread t, Method method, Locals.VariableDescription desc, int depth) {
        int real_slot = transform.applyAsInt(desc.slot);
        try {
          Object res = get.GetVar(t, depth, real_slot);
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

    public static class ThreadPauser implements Runnable {
      public final Semaphore sem_wakeup_main;
      public final Semaphore sem_wait;

      public ThreadPauser() {
        sem_wakeup_main = new Semaphore(0);
        sem_wait = new Semaphore(0);
      }

      public void run() {
        try {
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
      Thread remote =
          new Thread(
              () -> {
                try {
                  target.invoke(null, pause);
                } catch (Exception e) {
                  throw new Error("Error invoking remote thread " + Thread.currentThread(), e);
                }
              },
              "remote thread for " + target + " with " + safepoint);
      remote.start();
      pause.waitForOtherThreadToPause();
      try {
        Suspension.suspend(remote);
        StackTrace.StackFrameData frame = findStackFrame(remote);
        Locals.VariableDescription desc = findTargetVar(frame.current_location);
        safepoint.invoke(remote, target, desc, frame.depth);
      } finally {
        Suspension.resume(remote);
        pause.wakeupOtherThread();
        remote.join();
      }
    }

    private Locals.VariableDescription findTargetVar(long loc) {
      for (Locals.VariableDescription var : Locals.GetLocalVariableTable(target)) {
        if (var.start_location <= loc
            && var.length + var.start_location > loc
            && var.name.equals(TARGET_VAR)) {
          return var;
        }
      }
      throw new Error("Unable to find variable " + TARGET_VAR + " in " + target + " at loc " + loc);
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
    return Test1967.class.getDeclaredMethod(name, Runnable.class);
  }

  public static void run() throws Exception {
    Locals.EnableLocalVariableAccess();
    final TestCase[] MAIN_TEST_CASES =
        new TestCase[] {
          new TestCase(getMethod("IntMethod")),
          new TestCase(getMethod("LongMethod")),
          new TestCase(getMethod("ObjectMethod")),
        };

    final SafepointFunction[] SAFEPOINTS =
        new SafepointFunction[] {
          BadGet("Int_at_too_high", Locals::GetLocalVariableInt, (i) -> i + 100),
          BadGet("Long_at_too_high", Locals::GetLocalVariableLong, (i) -> i + 100),
          BadGet("Object_at_too_high", Locals::GetLocalVariableObject, (i) -> i + 100),
          BadSet("Int_at_too_high", Locals::SetLocalVariableInt, Integer.MAX_VALUE, (i) -> i + 100),
          BadSet("Long_at_too_high", Locals::SetLocalVariableLong, Long.MAX_VALUE, (i) -> i + 100),
          BadSet(
              "Object_at_too_high", Locals::SetLocalVariableObject, "NEW_FOR_SET", (i) -> i + 100),
          BadGet("Int_at_negative", Locals::GetLocalVariableInt, (i) -> -1),
          BadGet("Long_at_negative", Locals::GetLocalVariableLong, (i) -> -1),
          BadGet("Object_at_negative", Locals::GetLocalVariableObject, (i) -> -1),
          BadSet("Int_at_negative", Locals::SetLocalVariableInt, Integer.MAX_VALUE, (i) -> -1),
          BadSet("Long_at_negative", Locals::SetLocalVariableLong, Long.MAX_VALUE, (i) -> -1),
          BadSet("Object_at_negative", Locals::SetLocalVariableObject, "NEW_FOR_SET", (i) -> -1),
        };

    for (TestCase t : MAIN_TEST_CASES) {
      for (SafepointFunction s : SAFEPOINTS) {
        t.exec(s);
      }
    }
  }
}
