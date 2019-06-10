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

import static art.SuspendEvents.EVENT_TYPE_CLASS_LOAD;
import static art.SuspendEvents.setupFieldSuspendFor;
import static art.SuspendEvents.setupSuspendBreakpointFor;
import static art.SuspendEvents.setupSuspendClassEvent;
import static art.SuspendEvents.setupSuspendExceptionEvent;
import static art.SuspendEvents.setupSuspendMethodEvent;
import static art.SuspendEvents.setupSuspendPopFrameEvent;
import static art.SuspendEvents.setupSuspendSingleStepAt;
import static art.SuspendEvents.setupTest;
import static art.SuspendEvents.waitForSuspendHit;

import java.io.*;
import java.lang.reflect.Executable;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.concurrent.CountDownLatch;
import java.util.function.Consumer;
import java.util.function.Supplier;

public class Test1969 {
  public static final boolean PRINT_STACK_TRACE = false;

  public final boolean canRunClassLoadTests;

  public static void doNothing() {}

  public static interface TestSuspender {
    public void setupForceReturnRun(Thread thr);

    public void waitForSuspend(Thread thr);

    public void cleanup(Thread thr);

    public default void performForceReturn(Thread thr) {
      System.out.println("Will force return of " + thr.getName());
      NonStandardExit.forceEarlyReturnVoid(thr);
    }

    public default void setupNormalRun(Thread thr) {}
  }

  public static interface ThreadRunnable {
    public void run(Thread thr);
  }

  public static TestSuspender makeSuspend(final ThreadRunnable setup, final ThreadRunnable clean) {
    return new TestSuspender() {
      public void setupForceReturnRun(Thread thr) {
        setup.run(thr);
      }

      public void waitForSuspend(Thread thr) {
        waitForSuspendHit(thr);
      }

      public void cleanup(Thread thr) {
        clean.run(thr);
      }
    };
  }

  public void runTestOn(Supplier<Runnable> testObj, ThreadRunnable su, ThreadRunnable cl)
      throws Exception {
    runTestOn(testObj, makeSuspend(su, cl));
  }

  private static void SafePrintStackTrace(StackTraceElement st[]) {
    System.out.println(safeDumpStackTrace(st, "\t"));
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
      if (e.getClassName().equals("art.Test1969") && e.getMethodName().equals("runTests")) {
        os.println(prefix + "<Additional frames hidden>");
        break;
      }
    }
    os.flush();
    return baos.toString();
  }

  static long ID_COUNTER = 0;

  public Runnable Id(final Runnable tr) {
    final long my_id = ID_COUNTER++;
    return new Runnable() {
      public void run() {
        tr.run();
      }

      public String toString() {
        return "(ID: " + my_id + ") " + tr.toString();
      }
    };
  }

  public static long THREAD_COUNT = 0;

  public Thread mkThread(Runnable r) {
    Thread t = new Thread(r, "Test1969 target thread - " + THREAD_COUNT++);
    t.setUncaughtExceptionHandler(
        (thr, e) -> {
          System.out.println(
              "Uncaught exception in thread "
                  + thr
                  + " - "
                  + e.getClass().getName()
                  + ": "
                  + e.getLocalizedMessage());
          SafePrintStackTrace(e.getStackTrace());
        });
    return t;
  }

  final class TestConfig {
    public final Runnable testObj;
    public final TestSuspender suspender;

    public TestConfig(Runnable obj, TestSuspender su) {
      this.testObj = obj;
      this.suspender = su;
    }
  }

  public void runTestOn(Supplier<Runnable> testObjGen, TestSuspender su) throws Exception {
    runTestOn(() -> new TestConfig(testObjGen.get(), su));
  }

  public void runTestOn(Supplier<TestConfig> config) throws Exception {
    TestConfig normal_config = config.get();
    Runnable normal_run = Id(normal_config.testObj);
    try {
      System.out.println("NORMAL RUN: Single call with no interference on " + normal_run);
      Thread normal_thread = mkThread(normal_run);
      normal_config.suspender.setupNormalRun(normal_thread);
      normal_thread.start();
      normal_thread.join();
      System.out.println("NORMAL RUN: result for " + normal_run + " on " + normal_thread.getName());
    } catch (Exception e) {
      System.out.println("NORMAL RUN: Ended with exception for " + normal_run + "!");
      e.printStackTrace(System.out);
    }

    TestConfig force_return_config = config.get();
    Runnable testObj = Id(force_return_config.testObj);
    TestSuspender su = force_return_config.suspender;
    System.out.println("Single call with force-early-return on " + testObj);
    final CountDownLatch continue_latch = new CountDownLatch(1);
    final CountDownLatch startup_latch = new CountDownLatch(1);
    Runnable await =
        () -> {
          try {
            startup_latch.countDown();
            continue_latch.await();
          } catch (Exception e) {
            throw new Error("Failed to await latch", e);
          }
        };
    Thread thr =
        mkThread(
            () -> {
              await.run();
              testObj.run();
            });
    thr.start();

    // Wait until the other thread is started.
    startup_latch.await();

    // Setup suspension method on the thread.
    su.setupForceReturnRun(thr);

    // Let the other thread go.
    continue_latch.countDown();

    // Wait for the other thread to hit the breakpoint/watchpoint/whatever and
    // suspend itself
    // (without re-entering java)
    su.waitForSuspend(thr);

    // Cleanup the breakpoint/watchpoint/etc.
    su.cleanup(thr);

    try {
      // Pop the frame.
      su.performForceReturn(thr);
    } catch (Exception e) {
      System.out.println("Failed to force-return due to " + e);
      SafePrintStackTrace(e.getStackTrace());
    }

    // Start the other thread going again.
    Suspension.resume(thr);

    // Wait for the other thread to finish.
    thr.join();

    // See how many times calledFunction was called.
    System.out.println("result for " + testObj + " on " + thr.getName());
  }

  public abstract static class AbstractTestObject implements Runnable {
    public AbstractTestObject() {}

    public void run() {
      // This function should be force-early-returned.
      calledFunction();
    }

    public abstract void calledFunction();
  }

  public static class FieldBasedTestObject extends AbstractTestObject implements Runnable {
    public int TARGET_FIELD;
    public int cnt = 0;

    public FieldBasedTestObject() {
      super();
      TARGET_FIELD = 0;
    }

    public void calledFunction() {
      cnt++;
      // We put a watchpoint here and force-early-return when we are at it.
      TARGET_FIELD += 10;
      cnt++;
    }

    public String toString() {
      return "FieldBasedTestObject { TARGET_FIELD: " + TARGET_FIELD + ", cnt: " + cnt + " }";
    }
  }

  public static class StandardTestObject extends AbstractTestObject implements Runnable {
    public int cnt;

    public StandardTestObject() {
      super();
      cnt = 0;
    }

    public void calledFunction() {
      cnt++; // line +0
      // We put a breakpoint here and force-early-return when we are at it.
      doNothing(); // line +2
      cnt++; // line +3
      return; // line +4
    }

    public String toString() {
      return "StandardTestObject { cnt: " + cnt + " }";
    }
  }

  public static class SynchronizedFunctionTestObject extends AbstractTestObject
      implements Runnable {
    public int cnt;

    public SynchronizedFunctionTestObject() {
      super();
      cnt = 0;
    }

    public synchronized void calledFunction() {
      cnt++; // line +0
      // We put a breakpoint here and PopFrame when we are at it.
      doNothing(); // line +2
      cnt++; // line +3
      return;
    }

    public String toString() {
      return "SynchronizedFunctionTestObject { cnt: " + cnt + " }";
    }
  }

  public static class SynchronizedTestObject extends AbstractTestObject implements Runnable {
    public final Object lock;
    public int cnt;

    public SynchronizedTestObject() {
      super();
      lock = new Object();
      cnt = 0;
    }

    public void calledFunction() {
      synchronized (lock) { // line +0
        cnt++; // line +1
        // We put a breakpoint here and PopFrame when we are at it.
        doNothing(); // line +3
        cnt++; // line +4
        return; // line +5
      }
    }

    public String toString() {
      return "SynchronizedTestObject { cnt: " + cnt + " }";
    }
  }

  public static class ExceptionCatchTestObject extends AbstractTestObject implements Runnable {
    public static class TestError extends Error {}

    public int cnt;

    public ExceptionCatchTestObject() {
      super();
      cnt = 0;
    }

    public void calledFunction() {
      cnt++;
      try {
        doThrow();
        cnt += 100;
      } catch (TestError e) {
        System.out.println(e.getClass().getName() + " caught in called function.");
        cnt++;
      }
      return;
    }

    public Object doThrow() {
      throw new TestError();
    }

    public String toString() {
      return "ExceptionCatchTestObject { cnt: " + cnt + " }";
    }
  }

  public static class ExceptionThrowFarTestObject implements Runnable {
    public static class TestError extends Error {}

    public int cnt;
    public int baseCallCnt;
    public final boolean catchInCalled;

    public ExceptionThrowFarTestObject(boolean catchInCalled) {
      super();
      cnt = 0;
      baseCallCnt = 0;
      this.catchInCalled = catchInCalled;
    }

    public void run() {
      baseCallCnt++;
      try {
        callingFunction();
      } catch (TestError e) {
        System.out.println(e.getClass().getName() + " thrown and caught!");
      }
      baseCallCnt++;
    }

    public void callingFunction() {
      calledFunction();
    }

    public void calledFunction() {
      cnt++;
      if (catchInCalled) {
        try {
          cnt += 100;
          throw new TestError(); // We put a watch here.
        } catch (TestError e) {
          System.out.println(e.getClass().getName() + " caught in same function.");
          doNothing();
          cnt += 10;
          return;
        }
      } else {
        cnt++;
        throw new TestError(); // We put a watch here.
      }
    }

    public String toString() {
      return "ExceptionThrowFarTestObject { cnt: " + cnt + ", baseCnt: " + baseCallCnt + " }";
    }
  }

  public static class ExceptionOnceObject extends AbstractTestObject {
    public static final class TestError extends Error {}

    public int cnt;
    public final boolean throwInSub;

    public ExceptionOnceObject(boolean throwInSub) {
      super();
      cnt = 0;
      this.throwInSub = throwInSub;
    }

    public void calledFunction() {
      cnt++;
      if (cnt == 1) {
        if (throwInSub) {
          doThrow();
        } else {
          throw new TestError();
        }
      }
      return;
    }

    public void doThrow() {
      throw new TestError();
    }

    public String toString() {
      return "ExceptionOnceObject { cnt: " + cnt + ", throwInSub: " + throwInSub + " }";
    }
  }

  public static class ExceptionThrowTestObject implements Runnable {
    public static class TestError extends Error {}

    public int cnt;
    public int baseCallCnt;
    public final boolean catchInCalled;

    public ExceptionThrowTestObject(boolean catchInCalled) {
      super();
      cnt = 0;
      baseCallCnt = 0;
      this.catchInCalled = catchInCalled;
    }

    public void run() {
      baseCallCnt++;
      try {
        calledFunction();
      } catch (TestError e) {
        System.out.println(e.getClass().getName() + " thrown and caught!");
      }
      baseCallCnt++;
    }

    public void calledFunction() {
      cnt++;
      if (catchInCalled) {
        try {
          cnt += 10;
          throw new TestError(); // We put a watch here.
        } catch (TestError e) {
          System.out.println(e.getClass().getName() + " caught in same function.");
          doNothing();
          cnt += 100;
          return;
        }
      } else {
        cnt += 1;
        throw new TestError(); // We put a watch here.
      }
    }

    public String toString() {
      return "ExceptionThrowTestObject { cnt: " + cnt + ", baseCnt: " + baseCallCnt + " }";
    }
  }

  public static class NativeCalledObject extends AbstractTestObject {
    public int cnt = 0;

    public native void calledFunction();

    public String toString() {
      return "NativeCalledObject { cnt: " + cnt + " }";
    }
  }

  public static class NativeCallerObject implements Runnable {
    public Object returnValue = null;
    public int cnt = 0;

    public Object getReturnValue() {
      return returnValue;
    }

    public native void run();

    public void calledFunction() {
      cnt++;
      // We will stop using a MethodExit event.
      doNothing();
      cnt++;
      return;
    }

    public String toString() {
      return "NativeCallerObject { cnt: " + cnt + " }";
    }
  }

  public static class ClassLoadObject implements Runnable {
    public int cnt;

    public static final String[] CLASS_NAMES =
        new String[] {
          "Lart/Test1969$ClassLoadObject$TC0;",
          "Lart/Test1969$ClassLoadObject$TC1;",
          "Lart/Test1969$ClassLoadObject$TC2;",
          "Lart/Test1969$ClassLoadObject$TC3;",
          "Lart/Test1969$ClassLoadObject$TC4;",
          "Lart/Test1969$ClassLoadObject$TC5;",
          "Lart/Test1969$ClassLoadObject$TC6;",
          "Lart/Test1969$ClassLoadObject$TC7;",
          "Lart/Test1969$ClassLoadObject$TC8;",
          "Lart/Test1969$ClassLoadObject$TC9;",
        };

    private static int curClass = 0;

    private static class TC0 { public static int foo; static { foo = 100 + curClass; } }

    private static class TC1 { public static int foo; static { foo = 200 + curClass; } }

    private static class TC2 { public static int foo; static { foo = 300 + curClass; } }

    private static class TC3 { public static int foo; static { foo = 400 + curClass; } }

    private static class TC4 { public static int foo; static { foo = 500 + curClass; } }

    private static class TC5 { public static int foo; static { foo = 600 + curClass; } }

    private static class TC6 { public static int foo; static { foo = 700 + curClass; } }

    private static class TC7 { public static int foo; static { foo = 800 + curClass; } }

    private static class TC8 { public static int foo; static { foo = 900 + curClass; } }

    private static class TC9 { public static int foo; static { foo = 1000 + curClass; } }

    public ClassLoadObject() {
      super();
      cnt = 0;
    }

    public void run() {
      if (curClass == 0) {
        calledFunction0();
      } else if (curClass == 1) {
        calledFunction1();
      } else if (curClass == 2) {
        calledFunction2();
      } else if (curClass == 3) {
        calledFunction3();
      } else if (curClass == 4) {
        calledFunction4();
      } else if (curClass == 5) {
        calledFunction5();
      } else if (curClass == 6) {
        calledFunction6();
      } else if (curClass == 7) {
        calledFunction7();
      } else if (curClass == 8) {
        calledFunction8();
      } else if (curClass == 9) {
        calledFunction9();
      }
      curClass++;
    }

    public void calledFunction0() {
      cnt++;
      System.out.println("TC0.foo == " + TC0.foo);
    }

    public void calledFunction1() {
      cnt++;
      System.out.println("TC1.foo == " + TC1.foo);
    }

    public void calledFunction2() {
      cnt++;
      System.out.println("TC2.foo == " + TC2.foo);
    }

    public void calledFunction3() {
      cnt++;
      System.out.println("TC3.foo == " + TC3.foo);
    }

    public void calledFunction4() {
      cnt++;
      System.out.println("TC4.foo == " + TC4.foo);
    }

    public void calledFunction5() {
      cnt++;
      System.out.println("TC5.foo == " + TC5.foo);
    }

    public void calledFunction6() {
      cnt++;
      System.out.println("TC6.foo == " + TC6.foo);
    }

    public void calledFunction7() {
      cnt++;
      System.out.println("TC7.foo == " + TC7.foo);
    }

    public void calledFunction8() {
      cnt++;
      System.out.println("TC8.foo == " + TC8.foo);
    }

    public void calledFunction9() {
      cnt++;
      System.out.println("TC9.foo == " + TC9.foo);
    }

    public String toString() {
      return "ClassLoadObject { cnt: " + cnt + ", curClass: " + curClass + "}";
    }
  }

  public static class ObjectInitTestObject implements Runnable {
    // TODO How do we do this for <clinit>
    public int cnt = 0;
    public static final class ObjectInitTarget {
      public ObjectInitTarget(Runnable r) {
        super();  // line +0
        r.run(); // line +1
        // We set a breakpoint here and force-early-return
        doNothing();  // line +3
        r.run();  // line +4
      }
    }

    public void run() {
      new ObjectInitTarget(() -> cnt++);
    }

    public String toString() {
      return "ObjectInitTestObject { cnt: " + cnt + " }";
    }
  }

  public static class SuspendSuddenlyObject extends AbstractTestObject {
    public volatile boolean should_spin = true;
    public volatile boolean is_spinning = false;
    public int cnt = 0;

    public void calledFunction() {
      cnt++;
      do {
        is_spinning = true;
      } while (should_spin);
      cnt++;
      return;
    }

    public String toString() {
      return "SuspendSuddenlyObject { cnt: " + cnt + ", spun: " + is_spinning + " }";
    }
  }

  public static final class StaticMethodObject implements Runnable {
    public int cnt = 0;

    public static void calledFunction(Runnable incr) {
      incr.run();   // line +0
      // We put a breakpoint here to force the return.
      doNothing();  // line +2
      incr.run();   // line +3
      return;       // line +4
    }

    public void run() {
      calledFunction(() -> cnt++);
    }

    public final String toString() {
      return "StaticMethodObject { cnt: " + cnt + " }";
    }
  }

  // Only used by CTS to run without class-load tests.
  public static void run() throws Exception {
    new Test1969(false).runTests();
  }
  public static void run(boolean canRunClassLoadTests) throws Exception {
    new Test1969(canRunClassLoadTests).runTests();
  }

  public Test1969(boolean canRunClassLoadTests) {
    this.canRunClassLoadTests = canRunClassLoadTests;
  }

  public static void no_runTestOn(Supplier<Object> a, ThreadRunnable b, ThreadRunnable c) {}

  public void runTests() throws Exception {
    setupTest();

    final Method calledFunction = StandardTestObject.class.getDeclaredMethod("calledFunction");
    // Add a breakpoint on the second line after the start of the function
    final int line = Breakpoint.locationToLine(calledFunction, 0) + 2;
    final long loc = Breakpoint.lineToLocation(calledFunction, line);
    System.out.println("Test stopped using breakpoint");
    runTestOn(
        StandardTestObject::new,
        (thr) -> setupSuspendBreakpointFor(calledFunction, loc, thr),
        SuspendEvents::clearSuspendBreakpointFor);

    final Method syncFunctionCalledFunction =
        SynchronizedFunctionTestObject.class.getDeclaredMethod("calledFunction");
    // Add a breakpoint on the second line after the start of the function
    // Annoyingly r8 generally
    // has the first instruction (a monitor enter) not be marked as being on any
    // line but javac has
    // it marked as being on the first line of the function. Just use the second
    // entry on the
    // line-number table to get the breakpoint. This should be good for both.
    final long syncFunctionLoc =
        Breakpoint.getLineNumberTable(syncFunctionCalledFunction)[1].location;
    System.out.println("Test stopped using breakpoint with declared synchronized function");
    runTestOn(
        SynchronizedFunctionTestObject::new,
        (thr) -> setupSuspendBreakpointFor(syncFunctionCalledFunction, syncFunctionLoc, thr),
        SuspendEvents::clearSuspendBreakpointFor);

    final Method syncCalledFunction =
        SynchronizedTestObject.class.getDeclaredMethod("calledFunction");
    // Add a breakpoint on the second line after the start of the function
    final int syncLine = Breakpoint.locationToLine(syncCalledFunction, 0) + 3;
    final long syncLoc = Breakpoint.lineToLocation(syncCalledFunction, syncLine);
    System.out.println("Test stopped using breakpoint with synchronized block");
    runTestOn(
        SynchronizedTestObject::new,
        (thr) -> setupSuspendBreakpointFor(syncCalledFunction, syncLoc, thr),
        SuspendEvents::clearSuspendBreakpointFor);

    System.out.println("Test stopped on single step");
    runTestOn(
        StandardTestObject::new,
        (thr) -> setupSuspendSingleStepAt(calledFunction, loc, thr),
        SuspendEvents::clearSuspendSingleStepFor);

    final Field target_field = FieldBasedTestObject.class.getDeclaredField("TARGET_FIELD");
    System.out.println("Test stopped on field access");
    runTestOn(
        FieldBasedTestObject::new,
        (thr) -> setupFieldSuspendFor(FieldBasedTestObject.class, target_field, true, thr),
        SuspendEvents::clearFieldSuspendFor);

    System.out.println("Test stopped on field modification");
    runTestOn(
        FieldBasedTestObject::new,
        (thr) -> setupFieldSuspendFor(FieldBasedTestObject.class, target_field, false, thr),
        SuspendEvents::clearFieldSuspendFor);

    System.out.println("Test stopped during Method Exit of calledFunction");
    runTestOn(
        StandardTestObject::new,
        (thr) -> setupSuspendMethodEvent(calledFunction, /* enter */ false, thr),
        SuspendEvents::clearSuspendMethodEvent);

    System.out.println("Test stopped during Method Enter of calledFunction");
    runTestOn(
        StandardTestObject::new,
        (thr) -> setupSuspendMethodEvent(calledFunction, /* enter */ true, thr),
        SuspendEvents::clearSuspendMethodEvent);

    final Method exceptionOnceCalledMethod =
        ExceptionOnceObject.class.getDeclaredMethod("calledFunction");
    System.out.println("Test stopped during Method Exit due to exception thrown in same function");
    runTestOn(
        () -> new ExceptionOnceObject(/* throwInSub */ false),
        (thr) -> setupSuspendMethodEvent(exceptionOnceCalledMethod, /* enter */ false, thr),
        SuspendEvents::clearSuspendMethodEvent);

    System.out.println("Test stopped during Method Exit due to exception thrown in subroutine");
    runTestOn(
        () -> new ExceptionOnceObject(/* throwInSub */ true),
        (thr) -> setupSuspendMethodEvent(exceptionOnceCalledMethod, /* enter */ false, thr),
        SuspendEvents::clearSuspendMethodEvent);

    final Method exceptionThrowCalledMethod =
        ExceptionThrowTestObject.class.getDeclaredMethod("calledFunction");
    System.out.println(
        "Test stopped during notifyFramePop with exception on pop of calledFunction");
    runTestOn(
        () -> new ExceptionThrowTestObject(false),
        (thr) -> setupSuspendPopFrameEvent(0, exceptionThrowCalledMethod, thr),
        SuspendEvents::clearSuspendPopFrameEvent);

    final Method exceptionCatchThrowMethod =
        ExceptionCatchTestObject.class.getDeclaredMethod("doThrow");
    System.out.println("Test stopped during notifyFramePop with exception on pop of doThrow");
    runTestOn(
        ExceptionCatchTestObject::new,
        (thr) -> setupSuspendPopFrameEvent(0, exceptionCatchThrowMethod, thr),
        SuspendEvents::clearSuspendPopFrameEvent);

    System.out.println(
        "Test stopped during ExceptionCatch event of calledFunction "
            + "(catch in called function, throw in called function)");
    runTestOn(
        () -> new ExceptionThrowTestObject(true),
        (thr) -> setupSuspendExceptionEvent(exceptionThrowCalledMethod, /* catch */ true, thr),
        SuspendEvents::clearSuspendExceptionEvent);

    final Method exceptionCatchCalledMethod =
        ExceptionCatchTestObject.class.getDeclaredMethod("calledFunction");
    System.out.println(
        "Test stopped during ExceptionCatch event of calledFunction "
            + "(catch in called function, throw in subroutine)");
    runTestOn(
        ExceptionCatchTestObject::new,
        (thr) -> setupSuspendExceptionEvent(exceptionCatchCalledMethod, /* catch */ true, thr),
        SuspendEvents::clearSuspendExceptionEvent);

    System.out.println(
        "Test stopped during Exception event of calledFunction " + "(catch in calling function)");
    runTestOn(
        () -> new ExceptionThrowTestObject(false),
        (thr) -> setupSuspendExceptionEvent(exceptionThrowCalledMethod, /* catch */ false, thr),
        SuspendEvents::clearSuspendExceptionEvent);

    System.out.println(
        "Test stopped during Exception event of calledFunction (catch in called function)");
    runTestOn(
        () -> new ExceptionThrowTestObject(true),
        (thr) -> setupSuspendExceptionEvent(exceptionThrowCalledMethod, /* catch */ false, thr),
        SuspendEvents::clearSuspendExceptionEvent);

    final Method exceptionThrowFarCalledMethod =
        ExceptionThrowFarTestObject.class.getDeclaredMethod("calledFunction");
    System.out.println(
        "Test stopped during Exception event of calledFunction "
            + "(catch in parent of calling function)");
    runTestOn(
        () -> new ExceptionThrowFarTestObject(false),
        (thr) -> setupSuspendExceptionEvent(exceptionThrowFarCalledMethod, /* catch */ false, thr),
        SuspendEvents::clearSuspendExceptionEvent);

    System.out.println(
        "Test stopped during Exception event of calledFunction " + "(catch in called function)");
    runTestOn(
        () -> new ExceptionThrowFarTestObject(true),
        (thr) -> setupSuspendExceptionEvent(exceptionThrowFarCalledMethod, /* catch */ false, thr),
        SuspendEvents::clearSuspendExceptionEvent);

    System.out.println("Test stopped during random Suspend.");
    runTestOn(
        () -> {
          final SuspendSuddenlyObject sso = new SuspendSuddenlyObject();
          return new TestConfig(
              sso,
              new TestSuspender() {
                public void setupForceReturnRun(Thread thr) {}

                public void setupNormalRun(Thread thr) {
                  sso.should_spin = false;
                }

                public void waitForSuspend(Thread thr) {
                  while (!sso.is_spinning) {}
                  Suspension.suspend(thr);
                }

                public void cleanup(Thread thr) {}
              });
        });

    System.out.println("Test stopped during a native method fails");
    runTestOn(
        NativeCalledObject::new,
        SuspendEvents::setupWaitForNativeCall,
        SuspendEvents::clearWaitForNativeCall);

    System.out.println("Test stopped in a method called by native succeeds");
    final Method nativeCallerMethod = NativeCallerObject.class.getDeclaredMethod("calledFunction");
    runTestOn(
        NativeCallerObject::new,
        (thr) -> setupSuspendMethodEvent(nativeCallerMethod, /* enter */ false, thr),
        SuspendEvents::clearSuspendMethodEvent);


    System.out.println("Test stopped in a static method");
    final Method staticCalledMethod = StaticMethodObject.class.getDeclaredMethod("calledFunction", Runnable.class);
    final int staticFunctionLine= Breakpoint.locationToLine(staticCalledMethod, 0) + 2;
    final long staticFunctionLoc = Breakpoint.lineToLocation(staticCalledMethod, staticFunctionLine);
    runTestOn(
        StaticMethodObject::new,
        (thr) -> setupSuspendBreakpointFor(staticCalledMethod, staticFunctionLoc, thr),
        SuspendEvents::clearSuspendMethodEvent);

    System.out.println("Test stopped in a Object <init> method");
    final Executable initCalledMethod = ObjectInitTestObject.ObjectInitTarget.class.getConstructor(Runnable.class);
    final int initFunctionLine= Breakpoint.locationToLine(initCalledMethod, 0) + 3;
    final long initFunctionLoc = Breakpoint.lineToLocation(initCalledMethod, initFunctionLine);
    runTestOn(
        ObjectInitTestObject::new,
        (thr) -> setupSuspendBreakpointFor(initCalledMethod, initFunctionLoc, thr),
        SuspendEvents::clearSuspendMethodEvent);

    if (canRunClassLoadTests && CanRunClassLoadingTests()) {
      System.out.println("Test stopped during class-load.");
      runTestOn(
          ClassLoadObject::new,
          (thr) -> setupSuspendClassEvent(EVENT_TYPE_CLASS_LOAD, ClassLoadObject.CLASS_NAMES, thr),
          SuspendEvents::clearSuspendClassEvent);
      System.out.println("Test stopped during class-load.");
      runTestOn(
          ClassLoadObject::new,
          (thr) -> setupSuspendClassEvent(EVENT_TYPE_CLASS_LOAD, ClassLoadObject.CLASS_NAMES, thr),
          SuspendEvents::clearSuspendClassEvent);
    }
  }


  // Volatile is to prevent any future optimizations that could invalidate this test by doing
  // constant propagation and eliminating the failing paths before the verifier is able to load the
  // class.
  static volatile boolean ranClassLoadTest = false;
  static boolean classesPreverified = false;
  private static final class RCLT0 { public void foo() {} }
  private static final class RCLT1 { public void foo() {} }
  // If classes are not preverified for some reason (interp-ac, no-image, etc) the verifier will
  // actually load classes as it runs. This means that we cannot use the class-load tests as they
  // are written. TODO Support this.
  public boolean CanRunClassLoadingTests() {
    if (ranClassLoadTest) {
      return classesPreverified;
    }
    if (!ranClassLoadTest) {
      // Only this will ever be executed.
      new RCLT0().foo();
    } else {
      // This will never be executed. If classes are not preverified the verifier will load RCLT1
      // when the enclosing method is run. This behavior makes the class-load/prepare test cases
      // impossible to successfully run (they will deadlock).
      new RCLT1().foo();
      System.out.println("FAILURE: UNREACHABLE Location!");
    }
    classesPreverified = !isClassLoaded("Lart/Test1969$RCLT1;");
    ranClassLoadTest = true;
    return classesPreverified;
  }

  public static native boolean isClassLoaded(String name);
}
