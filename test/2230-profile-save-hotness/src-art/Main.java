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

import dalvik.system.VMRuntime;
import java.io.File;
import java.io.IOException;
import java.lang.reflect.Method;

public class Main {
  public static void $noinline$hotnessCount() {}

  public static void $noinline$hotnessCountWithLoop(int count) {
    for (int i = 0; i < count; i++) {
      $noinline$hotnessCount();
    }
  }

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    if (!isAotCompiled(Main.class, "main")) {
      return;
    }

    File file = null;
    try {
      file = createTempFile();
      String codePath = System.getenv("DEX_LOCATION") + "/2230-profile-save-hotness.jar";
      VMRuntime.registerAppInfo(file.getPath(), new String[] {codePath});

      // Test that the profile saves an app method with a profiling info.
      $noinline$hotnessCountWithLoop(10000);
      ensureProfileProcessing();
      String methodName = "$noinline$hotnessCount";
      Method appMethod = Main.class.getDeclaredMethod(methodName);
      if (!presentInProfile(file.getPath(), appMethod)) {
        System.out.println("App method not hot in profile " +
                getHotnessCounter(Main.class, methodName));
      }
      if (getHotnessCounter(Main.class, methodName) == 0) {
        System.out.println("Hotness should be non zero " +
                getHotnessCounter(Main.class, methodName));
      }
      VMRuntime.resetJitCounters();
      if (getHotnessCounter(Main.class, methodName) != 0) {
        System.out.println("Hotness should be zero " + getHotnessCounter(Main.class, methodName));
      }
    } finally {
      if (file != null) {
        file.delete();
      }
    }
  }

  // Checks if the profiles saver has the method as hot/warm.
  public static native boolean presentInProfile(String profile, Method method);
  // Ensures the profile saver does its usual processing.
  public static native void ensureProfileProcessing();
  public static native boolean isAotCompiled(Class<?> cls, String methodName);
  public static native int getHotnessCounter(Class<?> cls, String methodName);

  private static final String TEMP_FILE_NAME_PREFIX = "dummy";
  private static final String TEMP_FILE_NAME_SUFFIX = "-file";

  private static File createTempFile() throws Exception {
    try {
      return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
    } catch (IOException e) {
      System.setProperty("java.io.tmpdir", "/data/local/tmp");
      try {
        return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
      } catch (IOException e2) {
        System.setProperty("java.io.tmpdir", "/sdcard");
        return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
      }
    }
  }
}
