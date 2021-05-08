/*
 * Copyright (C) 2021 The Android Open Source Project
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

import dalvik.system.VMRuntime;
import java.lang.ref.WeakReference;
import java.nio.ByteBuffer;

public class Main {

  static final int HOW_MANY_HUGE = 110;  // > 1GB to trigger blocking in default config.
  int allocated = 0;
  int deallocated = 0;
  static Object lock = new Object();
  WeakReference<BufferHolder>[] references = new WeakReference[HOW_MANY_HUGE];

  class BufferHolder {
    private ByteBuffer buffer;
    BufferHolder() {
      ++allocated;
      buffer = getHugeNativeBuffer();
    }
    protected void finalize() {
      synchronized(lock) {
        ++deallocated;
      }
      deleteHugeNativeBuffer(buffer);
      buffer = null;
    }
  }

  // Repeatedly inform the GC of native allocations. Return the time (in nsecs) this takes.
  private static long timeNotifications() {
    VMRuntime vmr = VMRuntime.getRuntime();
    long startNanos = System.nanoTime();
    for (int i = 0; i < 200; ++i) {
      vmr.notifyNativeAllocation();
    }
    return System.nanoTime() - startNanos;
  }

  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    System.out.println("Main Started");
    new Main().run();
    System.out.println("Main Finished");
  }

  void run() {
    timeNotifications();  // warm up.
    long referenceTime1 = timeNotifications();
    long referenceTime2 = timeNotifications();
    long referenceTime = Math.min(referenceTime1, referenceTime2);

    // Allocate half a GB of native memory without informing the GC.
    for (int i = 0; i < HOW_MANY_HUGE; ++i) {
      new BufferHolder();
    }

    // One of the notifications should block for GC to catch up.
    long actualTime = timeNotifications();

    if (actualTime > 500_000_000) {
      System.out.println("Notifications ran too slowly; excessive blocking? msec = "
          + (actualTime / 1_000_000));
    } else if (actualTime < 3 * referenceTime + 2_000_000) {
      System.out.println("Notifications ran too quickly; no blocking GC? msec = "
          + (actualTime / 1_000_000));
    }

    // Let finalizers run.
    try {
      Thread.sleep(3000);
    } catch (InterruptedException e) {
      System.out.println("Unexpected interrupt");
    }

    if (deallocated > allocated || deallocated < allocated - 5 /* slop for register references */) {
      System.out.println("Unexpected number of deallocated objects:");
      System.out.println("Allocated = " + allocated + " deallocated = " + deallocated);
    }
  }

  private static native ByteBuffer getHugeNativeBuffer();
  private static native void deleteHugeNativeBuffer(ByteBuffer buf);
}
