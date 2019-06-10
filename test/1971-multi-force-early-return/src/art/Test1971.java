/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */

package art;

import java.io.*;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.concurrent.CountDownLatch;
import java.util.function.Consumer;
import java.util.function.IntConsumer;
import java.util.function.Supplier;

public class Test1971 {
  public static final boolean PRINT_STACK_TRACE = false;
  public static final int NUM_THREADS = 3;

  public static class ReturnValue {
    public final Thread target;
    public final Thread creator;
    public final Thread.State state;
    public final StackTraceElement stack[];

    public ReturnValue(Thread thr) {
      target = thr;
      creator = Thread.currentThread();
      state = thr.getState();
      stack = thr.getStackTrace();
    }

    public String toString() {
      String stackTrace =
          PRINT_STACK_TRACE
              ?  ",\n\tstate: "
                  + state
                  + ",\n\tstack:\n"
                  + safeDumpStackTrace(stack, "\t\t")
                  + ",\n\t"
              : "";
      return this.getClass().getName()
                  + " { thread: " + target.getName()
                  + ", creator: " + creator.getName()
                  + stackTrace + " }";
    }
  }

  private static String safeDumpStackTrace(StackTraceElement st[], String prefix) {
    ByteArrayOutputStream baos = new ByteArrayOutputStream();
    PrintStream os = new PrintStream(baos);
    for (StackTraceElement e : st) {
      os.println(
          prefix
              + e.getClassName()
              + "."
              + e.getMethodName()
              + "("
              + (e.isNativeMethod() ? "Native Method" : e.getFileName())
              + ")");
      if (e.getClassName().equals("art.Test1971") && e.getMethodName().equals("runTests")) {
        os.println(prefix + "<Additional frames hidden>");
        break;
      }
    }
    os.flush();
    return baos.toString();
  }

  public static final class ForcedExit extends ReturnValue {
    public ForcedExit(Thread thr) {
      super(thr);
    }
  }

  public static final class NormalExit extends ReturnValue {
    public NormalExit() {
      super(Thread.currentThread());
    }
  }

  public static void runTest(Consumer<String> con) {
    String thread_name = Thread.currentThread().getName();
    con.accept("Thread: " + thread_name + " method returned: " + targetMethod());
  }
  public static Object targetMethod() {
    // Set a breakpoint here and perform a force-early-return
    return new NormalExit();
  }

  public static void run() throws Exception {
    SuspendEvents.setupTest();

    final String[] results = new String[NUM_THREADS];
    final Thread[] targets = new Thread[NUM_THREADS];
    final CountDownLatch cdl = new CountDownLatch(1);
    final CountDownLatch startup = new CountDownLatch(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; i++) {
      final int idx = i;
      targets[i] = new Thread(() -> {
        try {
          startup.countDown();
          cdl.await();
          runTest((s) -> {
            synchronized(results) {
              results[idx] = s;
            }
          });
        } catch (Exception e) {
          throw new Error("Failed to run test!", e);
        }
      }, "Test1971 - Thread " + i);
      targets[i].start();
    }
    // Wait for the targets to start.
    startup.await();
    final Method targetMethod = Test1971.class.getDeclaredMethod("targetMethod");
    final long targetLoc = 0;
    // Setup breakpoints on all targets.
    for (Thread thr : targets) {
      try {
        SuspendEvents.setupSuspendBreakpointFor(targetMethod, targetLoc, thr);
      } catch (RuntimeException e) {
        if (e.getMessage().equals("JVMTI_ERROR_DUPLICATE")) {
          continue;
        } else {
          throw e;
        }
      }
    }
    // Allow tests to continue.
    cdl.countDown();
    // Wait for breakpoint to be hit on all threads.
    for (Thread thr : targets) {
      SuspendEvents.waitForSuspendHit(thr);
    }
    final CountDownLatch force_return_start = new CountDownLatch(NUM_THREADS);
    final CountDownLatch force_return_latch = new CountDownLatch(1);
    Thread[] returners = new Thread[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
      final int idx = i;
      final Thread target = targets[i];
      returners[i] = new Thread(() -> {
        try {
          force_return_start.countDown();
          force_return_latch.await();
          if (idx % 5 != 0) {
            NonStandardExit.forceEarlyReturn(target, new ForcedExit(target));
          }
          Suspension.resume(target);
        } catch (Exception e) {
          throw new Error("Failed to resume!", e);
        }
      }, "Concurrent thread force-returner - " + i);
      returners[i].start();
    }
    // Force-early-return and resume on all threads simultaneously.
    force_return_start.await();
    force_return_latch.countDown();

    // Wait for all threads to finish.
    for (int i = 0; i < NUM_THREADS; i++) {
      returners[i].join();
      targets[i].join();
    }

    // Print results
    for (int i = 0; i < NUM_THREADS; i++) {
      System.out.println("Thread " + i + ": " + results[i]);
    }
  }
}
