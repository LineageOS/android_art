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
import java.util.concurrent.atomic.AtomicInteger;

public class Main {

  private final boolean PRINT_TIMES = false;  // False for use as run test.

  // Number of increments done by each thread.  Must be multiple of largest hold time below,
  // times any possible thread count. Finishes much faster when used as run test.
  private final int TOTAL_ITERS = PRINT_TIMES? 16_000_000 : 1_600_000;
  private final int MAX_HOLD_TIME = PRINT_TIMES? 2_000_000 : 200_000;

  private int counter;

  private AtomicInteger atomicCounter = new AtomicInteger();

  private Object lock;

  private int currentThreadCount = 0;

  // A function such that if we repeatedly apply it to -1, the value oscillates
  // between -1 and 3. Thus the average value is 1.
  // This is designed to make it hard for the compiler to predict the values in
  // the sequence.
  private int nextInt(int x) {
    if (x < 0) {
      return x * x + 2;
    } else {
      return x - 4;
    }
  }

  // Increment counter by n, holding log for time roughly propertional to n.
  // N must be even.
  private void holdFor(Object lock, int n) {
    synchronized(lock) {
      int y = -1;
      for (int i = 0; i < n; ++i) {
        counter += y;
        y = nextInt(y);
      }
    }
  }

  private class RepeatedLockHolder implements Runnable {
    RepeatedLockHolder(boolean shared, int n /* even */) {
      sharedLock = shared;
      holdTime = n;
    }
    @Override
    public void run() {
      Object myLock = sharedLock ? lock : new Object();
      int nIters = TOTAL_ITERS / currentThreadCount / holdTime;
      for (int i = 0; i < nIters; ++i) {
        holdFor(myLock, holdTime);
      }
    }
    private boolean sharedLock;
    private int holdTime;
  }

  private class SleepyLockHolder implements Runnable {
    SleepyLockHolder(boolean shared) {
      sharedLock = shared;
    }
    @Override
    public void run() {
      Object myLock = sharedLock ? lock : new Object();
      int nIters = TOTAL_ITERS / currentThreadCount / 10_000;
      for (int i = 0; i < nIters; ++i) {
        synchronized(myLock) {
          try {
            Thread.sleep(2);
          } catch(InterruptedException e) {
            throw new AssertionError("Unexpected interrupt");
          }
          counter += 10_000;
        }
      }
    }
    private boolean sharedLock;
  }

  // Increment atomicCounter n times, on average by 1 each time.
  private class RepeatedIncrementer implements Runnable {
    @Override
    public void run() {
      int y = -1;
      int nIters = TOTAL_ITERS / currentThreadCount;
      for (int i = 0; i < nIters; ++i) {
        atomicCounter.addAndGet(y);
        y = nextInt(y);
      }
    }
  }

  // Run n threads doing work. Return the elapsed time this took, in milliseconds.
  private long runMultiple(int n, Runnable work) {
    Thread[] threads = new Thread[n];
    // Replace lock, so that we start with a clean, uninflated lock each time.
    lock = new Object();
    for (int i = 0; i < n; ++i) {
      threads[i] = new Thread(work);
    }
    long startTime = System.currentTimeMillis();
    for (int i = 0; i < n; ++i) {
      threads[i].start();
    }
    for (int i = 0; i < n; ++i) {
      try {
        threads[i].join();
      } catch(InterruptedException e) {
        throw new AssertionError("Unexpected interrupt");
      }
    }
    return System.currentTimeMillis() - startTime;
  }

  // Run on different numbers of threads.
  private void runAll(Runnable work, Runnable init, Runnable checker) {
    for (int i = 1; i <= 8; i *= 2) {
      currentThreadCount = i;
      init.run();
      long time = runMultiple(i, work);
      if (PRINT_TIMES) {
        System.out.print(time + (i == 8 ? "\n" : "\t"));
      }
      checker.run();
    }
  }

  private class CheckAtomicCounter implements Runnable {
    @Override
    public void run() {
      if (atomicCounter.get() != TOTAL_ITERS) {
        throw new AssertionError("Failed atomicCounter postcondition check for "
            + currentThreadCount + " threads");
      }
    }
  }

  private class CheckCounter implements Runnable {
    @Override
    public void run() {
      if (counter != TOTAL_ITERS) {
        throw new AssertionError("Failed counter postcondition check for "
            + currentThreadCount + " threads");
      }
    }
  }

  private void run() {
    if (PRINT_TIMES) {
      System.out.println("All times in milliseconds for 1, 2, 4 and 8 threads");
    }
    System.out.println("Atomic increments");
    runAll(new RepeatedIncrementer(), () -> { atomicCounter.set(0); }, new CheckAtomicCounter());
    for (int i = 2; i <= MAX_HOLD_TIME; i *= 10) {
      // i * 8 (max thread count) divides TOTAL_ITERS
      System.out.println("Hold time " + i + ", shared lock");
      runAll(new RepeatedLockHolder(true, i), () -> { counter = 0; }, new CheckCounter());
    }
    if (PRINT_TIMES) {
      for (int i = 2; i <= MAX_HOLD_TIME; i *= 1000) {
        // i divides TOTAL_ITERS
        System.out.println("Hold time " + i + ", private lock");
        // Since there is no mutual exclusion final counter value is unpredictable.
        runAll(new RepeatedLockHolder(false, i), () -> { counter = 0; }, () -> {});
      }
    }
    System.out.println("Hold for 2 msecs while sleeping, shared lock");
    runAll(new SleepyLockHolder(true), () -> { counter = 0; }, new CheckCounter());
    System.out.println("Hold for 2 msecs while sleeping, private lock");
    runAll(new SleepyLockHolder(false), () -> { counter = 0; }, () -> {});
  }

  public static void main(String[] args) {
    System.out.println("Starting");
    new Main().run();
  }
}
