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

public class Main {

  private static class Child implements Runnable {
    @Override
    public void run() {
      System.out.println("Child started");
      // Enter native method and stay there, monitoring shutdown behavior.  Since we're a daemon,
      // the process should shut down anyway, and we should be able to observe changes in the
      // extended JNI environment.
      monitorShutdown();
    }
  }

  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    System.out.println("Main Started");
    Thread t = new Thread(new Child());
    t.setDaemon(true);
    t.start();
    try {
      Thread.sleep(400);
    } catch (InterruptedException e) {
      System.out.println("Unexpected interrupt");
    }
    System.out.println("Main Finished");
  }

  private static native void monitorShutdown();
}
