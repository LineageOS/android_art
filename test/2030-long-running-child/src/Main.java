/*
 * Copyright (C) 2020 The Android Open Source Project
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

  private static class LazyGrandChildThread implements Runnable {
    @Override
    public void run() {}
  }

  private static class ChildThread implements Runnable {
    @Override
    public void run() {
      // Allocate memory forcing GCs and fork children.
      for (int i = 0; i < 100; ++i) {
        int [][] a = new int[10][];
        for (int j = 0; j < 10; ++j) {
          a[j] = new int[50000 * j + 20];
          a[j][17] = 1;
        }
        Thread t = new Thread(new LazyGrandChildThread());
        t.start();
        int sum = 0;
        // Make it hard to optimize out the arrays.
        for (int j = 0; j < 10; ++j) {
          sum += a[j][16] /* = 0 */ + a[j][17] /* = 1 */;
        }
        if (sum != 10) {
          System.out.println("Bad result! Was " + sum);
        }
        try {
          t.join();
        } catch (InterruptedException e) {
          System.out.println("Interrupted by " + e);
        }
      }
      System.out.println("Child finished");
    }
  }

  public static void main(String[] args) {
    System.out.println("Main Started");
    new Thread(new ChildThread()).start();
    System.out.println("Main Finished");
  }
}
