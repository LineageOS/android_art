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

import static art.SuspendEvents.setupFieldSuspendFor;
import static art.SuspendEvents.setupSuspendBreakpointFor;
import static art.SuspendEvents.setupSuspendExceptionEvent;
import static art.SuspendEvents.setupSuspendMethodEvent;
import static art.SuspendEvents.setupSuspendPopFrameEvent;
import static art.SuspendEvents.setupSuspendSingleStepAt;
import static art.SuspendEvents.setupTest;
import static art.SuspendEvents.waitForSuspendHit;

import java.io.*;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.concurrent.CountDownLatch;
import java.util.function.Consumer;
import java.util.function.Supplier;

public class Test1970 {
  // Make sure this is always high enough that it's easily distinguishable from the results the
  // methods would normally return.
  public static long OVERRIDE_ID = 987000;

  // Returns a value to be used for the return value of the given thread.
  public static long getOveriddenReturnValue(Thread thr) {
    return OVERRIDE_ID++;
  }

  public static void doNothing() {}

  public interface TestRunnable extends Runnable {
    public long getReturnValue();
  }

  public static interface TestSuspender {
    public void setupForceReturnRun(Thread thr);

    public void waitForSuspend(Thread thr);

    public void cleanup(Thread thr);

    public default void performForceReturn(Thread thr) {
      long ret = getOveriddenReturnValue(thr);
      System.out.println("Will force return of " + ret);
      NonStandardExit.forceEarlyReturn(thr, ret);
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

  public void runTestOn(Supplier<TestRunnable> testObj, ThreadRunnable su, ThreadRunnable cl)
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
      if (e.getClassName().equals("art.Test1970") && e.getMethodName().equals("runTests")) {
        os.println(prefix + "<Additional frames hidden>");
        break;
      }
    }
    os.flush();
    return baos.toString();
  }

  static long ID_COUNTER = 0;

  public TestRunnable Id(final TestRunnable tr) {
    final long my_id = ID_COUNTER++;
    return new TestRunnable() {
      public void run() {
        tr.run();
      }

      public long getReturnValue() {
        return tr.getReturnValue();
      }

      public String toString() {
        return "(ID: " + my_id + ") " + tr.toString();
      }
    };
  }

  public static long THREAD_COUNT = 0;

  public Thread mkThread(Runnable r) {
    Thread t = new Thread(r, "Test1970 target thread - " + THREAD_COUNT++);
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
    public final TestRunnable testObj;
    public final TestSuspender suspender;

    public TestConfig(TestRunnable obj, TestSuspender su) {
      this.testObj = obj;
      this.suspender = su;
    }
  }

  public void runTestOn(Supplier<TestRunnable> testObjGen, TestSuspender su) throws Exception {
    runTestOn(() -> new TestConfig(testObjGen.get(), su));
  }

  public void runTestOn(Supplier<TestConfig> config) throws Exception {
    TestConfig normal_config = config.get();
    TestRunnable normal_run = Id(normal_config.testObj);
    try {
      System.out.println("NORMAL RUN: Single call with no interference on " + normal_run);
      Thread normal_thread = mkThread(normal_run);
      normal_config.suspender.setupNormalRun(normal_thread);
      normal_thread.start();
      normal_thread.join();
      System.out.println(
          "NORMAL RUN: result for " + normal_run + " is " + normal_run.getReturnValue());
    } catch (Exception e) {
      System.out.println("NORMAL RUN: Ended with exception for " + normal_run + "!");
      e.printStackTrace(System.out);
    }

    TestConfig force_return_config = config.get();
    TestRunnable testObj = Id(force_return_config.testObj);
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
    System.out.println("result for " + testObj + " is " + testObj.getReturnValue());
  }

  public abstract static class AbstractTestObject implements TestRunnable {
    private long resultVal = 0;

    public AbstractTestObject() { }

    public long getReturnValue() {
      return resultVal;
    }

    public void run() {
      // This function should have it's return-value replaced by force-early-return.
      resultVal = calledFunction();
    }

    public abstract long calledFunction();
  }

  public static class IntContainer {
    private final int value;

    public IntContainer(int i) {
      value = i;
    }

    public String toString() {
      return "IntContainer { value: " + value + " }";
    }
  }

  public static class FieldBasedTestObject extends AbstractTestObject implements Runnable {
    public int TARGET_FIELD;

    public FieldBasedTestObject() {
      super();
      TARGET_FIELD = 0;
    }

    public long calledFunction() {
      // We put a watchpoint here and force-early-return when we are at it.
      TARGET_FIELD += 10;
      return TARGET_FIELD;
    }

    public String toString() {
      return "FieldBasedTestObject { TARGET_FIELD: " + TARGET_FIELD + " }";
    }
  }

  public static class StandardTestObject extends AbstractTestObject implements Runnable {
    public int cnt;

    public StandardTestObject() {
      super();
      cnt = 0;
    }

    public long calledFunction() {
      cnt++; // line +0
      // We put a breakpoint here and PopFrame when we are at it.
      long result = cnt; // line +2
      cnt++; // line +3
      return result; // line +4
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

    public synchronized long calledFunction() {
      cnt++; // line +0
      // We put a breakpoint here and PopFrame when we are at it.
      long result = cnt; // line +2
      cnt++; // line +3
      return result;
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

    public long calledFunction() {
      synchronized (lock) { // line +0
        cnt++; // line +1
        // We put a breakpoint here and PopFrame when we are at it.
        long result = cnt; // line +3
        cnt++; // line +4
        return result; // line +5
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

    public long calledFunction() {
      cnt++;
      long result = cnt;
      try {
        doThrow();
        cnt += 100;
      } catch (TestError e) {
        System.out.println(e.getClass().getName() + " caught in called function.");
        cnt++;
      }
      return result;
    }

    public Object doThrow() {
      throw new TestError();
    }

    public String toString() {
      return "ExceptionCatchTestObject { cnt: " + cnt + " }";
    }
  }

  public static class ExceptionThrowFarTestObject implements TestRunnable {
    public static class TestError extends Error {}

    public int cnt;
    public int baseCallCnt;
    public final boolean catchInCalled;
    public long result;

    public ExceptionThrowFarTestObject(boolean catchInCalled) {
      super();
      cnt = 0;
      baseCallCnt = 0;
      this.catchInCalled = catchInCalled;
    }

    public void run() {
      baseCallCnt++;
      try {
        result = callingFunction();
      } catch (TestError e) {
        System.out.println(e.getClass().getName() + " thrown and caught!");
      }
      baseCallCnt++;
    }

    public long callingFunction() {
      return calledFunction();
    }

    public long calledFunction() {
      cnt++;
      if (catchInCalled) {
        try {
          cnt += 100;
          throw new TestError(); // We put a watch here.
        } catch (TestError e) {
          System.out.println(e.getClass().getName() + " caught in same function.");
          long result = cnt;
          cnt += 10;
          return result;
        }
      } else {
        cnt++;
        throw new TestError(); // We put a watch here.
      }
    }

    public String toString() {
      return "ExceptionThrowFarTestObject { cnt: " + cnt + ", baseCnt: " + baseCallCnt + " }";
    }

    @Override
    public long getReturnValue() {
      return result;
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

    public long calledFunction() {
      cnt++;
      if (cnt == 1) {
        if (throwInSub) {
          return doThrow();
        } else {
          throw new TestError();
        }
      }
      return cnt++;
    }

    public long doThrow() {
      throw new TestError();
    }

    public String toString() {
      return "ExceptionOnceObject { cnt: " + cnt + ", throwInSub: " + throwInSub + " }";
    }
  }

  public static class ExceptionThrowTestObject implements TestRunnable {
    public static class TestError extends Error {}

    public long getReturnValue() {
      return result;
    }

    public int cnt;
    public int baseCallCnt;
    public final boolean catchInCalled;
    public long result;

    public ExceptionThrowTestObject(boolean catchInCalled) {
      super();
      cnt = 0;
      baseCallCnt = 0;
      this.catchInCalled = catchInCalled;
    }

    public void run() {
      baseCallCnt++;
      try {
        result = calledFunction();
      } catch (TestError e) {
        System.out.println(e.getClass().getName() + " thrown and caught!");
      }
      baseCallCnt++;
    }

    public long calledFunction() {
      cnt++;
      if (catchInCalled) {
        try {
          cnt += 10;
          throw new TestError(); // We put a watch here.
        } catch (TestError e) {
          System.out.println(e.getClass().getName() + " caught in same function.");
          long result = cnt;
          cnt += 100;
          return result;
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

    public native long calledFunction();

    public String toString() {
      return "NativeCalledObject { cnt: " + cnt + " }";
    }
  }

  public static class NativeCallerObject implements TestRunnable {
    public long returnValue = -1;
    public int cnt = 0;

    public long getReturnValue() {
      return returnValue;
    }

    public native void run();

    public long calledFunction() {
      cnt++;
      // We will stop using a MethodExit event.
      long res = cnt;
      cnt++;
      return res;
    }

    public String toString() {
      return "NativeCallerObject { cnt: " + cnt + " }";
    }
  }

  public static class StaticMethodObject implements TestRunnable {
    public int cnt = 0;
    public long result = -1;
    public long getReturnValue() {
      return result;
    }

    public static long calledFunction(Supplier<Long> incr) {
      long res = incr.get().longValue(); // line +0
      // We put a breakpoint here to force the return.
      doNothing();  // line +2
      incr.get();   // line +3
      return res;   // line +4
    }

    public void run() {
      result = calledFunction(() -> (long)++cnt);
    }

    public String toString() {
      return "StaticMethodObject { cnt: " + cnt + " }";
    }
  }

  public static class SuspendSuddenlyObject extends AbstractTestObject {
    public volatile boolean should_spin = true;
    public volatile boolean is_spinning = false;
    public int cnt = 0;

    public long calledFunction() {
      cnt++;
      do {
        is_spinning = true;
      } while (should_spin);
      return cnt++;
    }

    public String toString() {
      return "SuspendSuddenlyObject { cnt: " + cnt + ", spun: " + is_spinning + " }";
    }
  }

  public static class BadForceVoidObject implements TestRunnable {
    public int cnt = 0;
    public long getReturnValue() {
      return -1;
    }
    public void run() {
      incrCnt();
    }
    public void incrCnt() {
      ++cnt;  // line +0
      // We set a breakpoint here and try to force-early-return.
      doNothing(); // line +2
      ++cnt;  // line +3
    }
    public String toString() {
      return "BadForceVoidObject { cnt: " + cnt + " }";
    }
  }

  public static class BadForceObjectObject implements TestRunnable {
    public int cnt = 0;
    public Long result = null;
    public long getReturnValue() {
      return result.longValue();
    }
    public void run() {
      result = incrCnt();
    }
    public Long incrCnt() {
      ++cnt;  // line +0
      // We set a breakpoint here and try to force-early-return.
      Long res = Long.valueOf(cnt);  // line +2
      ++cnt;  // line +3
      return res;
    }
    public String toString() {
      return "BadForceIntObject { cnt: " + cnt + " }";
    }
  }
  public static class BadForceIntObject implements TestRunnable {
    public int cnt = 0;
    public int result = 0;
    public long getReturnValue() {
      return result;
    }
    public void run() {
      result = incrCnt();
    }
    public int incrCnt() {
      ++cnt;  // line +0
      // We set a breakpoint here and try to force-early-return.
      int res = cnt;  // line +2
      ++cnt;  // line +3
      return res;
    }
    public String toString() {
      return "BadForceIntObject { cnt: " + cnt + " }";
    }
  }

  public static void run() throws Exception {
    new Test1970().runTests();
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
    // Add a breakpoint on the second line after the start of the function Annoyingly r8 generally
    // has the first instruction (a monitor enter) not be marked as being on any line but javac has
    // it marked as being on the first line of the function. Just use the second entry on the
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
    runTestOn(() -> {
      final SuspendSuddenlyObject sso = new SuspendSuddenlyObject();
      return new TestConfig(sso, new TestSuspender() {
        public void setupForceReturnRun(Thread thr) { }
        public void setupNormalRun(Thread thr) {
          sso.should_spin = false;
        }

        public void waitForSuspend(Thread thr) {
          while (!sso.is_spinning) { }
          Suspension.suspend(thr);
        }

        public void cleanup(Thread thr) { }
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
    final Method staticCalledMethod = StaticMethodObject.class.getDeclaredMethod("calledFunction", Supplier.class);
    final int staticFunctionLine= Breakpoint.locationToLine(staticCalledMethod, 0) + 2;
    final long staticFunctionLoc = Breakpoint.lineToLocation(staticCalledMethod, staticFunctionLine);
    runTestOn(
        StaticMethodObject::new,
        (thr) -> setupSuspendBreakpointFor(staticCalledMethod, staticFunctionLoc, thr),
        SuspendEvents::clearSuspendMethodEvent);

    System.out.println("Test force-return of void function fails!");
    final Method voidFunction = BadForceVoidObject.class.getDeclaredMethod("incrCnt");
    final int voidLine = Breakpoint.locationToLine(voidFunction, 0) + 2;
    final long voidLoc = Breakpoint.lineToLocation(voidFunction, voidLine);
    runTestOn(
        BadForceVoidObject::new,
        (thr) -> setupSuspendBreakpointFor(voidFunction, voidLoc, thr),
        SuspendEvents::clearSuspendMethodEvent);

    System.out.println("Test force-return of int function fails!");
    final Method intFunction = BadForceIntObject.class.getDeclaredMethod("incrCnt");
    final int intLine = Breakpoint.locationToLine(intFunction, 0) + 2;
    final long intLoc = Breakpoint.lineToLocation(intFunction, intLine);
    runTestOn(
        BadForceIntObject::new,
        (thr) -> setupSuspendBreakpointFor(intFunction, intLoc, thr),
        SuspendEvents::clearSuspendMethodEvent);

    System.out.println("Test force-return of Object function fails!");
    final Method objFunction = BadForceObjectObject.class.getDeclaredMethod("incrCnt");
    final int objLine = Breakpoint.locationToLine(objFunction, 0) + 2;
    final long objLoc = Breakpoint.lineToLocation(objFunction, objLine);
    runTestOn(
        BadForceObjectObject::new,
        (thr) -> setupSuspendBreakpointFor(objFunction, objLoc, thr),
        SuspendEvents::clearSuspendMethodEvent);
  }
}
