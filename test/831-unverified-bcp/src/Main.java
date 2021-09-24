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

import dalvik.system.PathClassLoader;
import java.io.File;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.util.Arrays;

public class Main {

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    appendToBootClassLoader(OTHER_DEX, /* isCorePlatform */ false);

    try {
      Class.forName("NonVerifiedClass");
      throw new Error("Expected VerifyError");
    } catch (VerifyError e) {
      // Expected.
    }
  }

  private static native int appendToBootClassLoader(String dexPath, boolean isCorePlatform);

  private static final String OTHER_DEX =
      new File(System.getenv("DEX_LOCATION"), "831-unverified-bcp-ex.jar").getAbsolutePath();
}

// Define the class also in the classpath, to trigger the AssertNoPendingException crash.
class NonVerifiedClass {
}
