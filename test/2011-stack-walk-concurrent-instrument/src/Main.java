/*
 * Copyright 2020 The Android Open Source Project
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

import java.util.concurrent.*;

public class Main {
  public Main() {
  }

  void $noinline$f(Runnable r) throws Exception {
    $noinline$g(r);
  }

  void $noinline$g(Runnable r) {
    $noinline$h(r);
  }

  void $noinline$h(Runnable r) {
    r.run();
  }

  public native void resetTest();
  public native void waitAndDeopt(Thread t);
  public native void doSelfStackWalk();

  void testConcurrent() throws Exception {
    resetTest();
    final Thread current = Thread.currentThread();
    Thread t = new Thread(() -> {
      try {
        this.waitAndDeopt(current);
      } catch (Exception e) {
        throw new Error("Fail!", e);
      }
    });
    t.start();
    $noinline$f(() -> {
      try {
        this.doSelfStackWalk();
      } catch (Exception e) {
        throw new Error("Fail!", e);
      }
    });
    t.join();
  }

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    Main st = new Main();
    st.testConcurrent();
    System.out.println("Done");
  }
}
