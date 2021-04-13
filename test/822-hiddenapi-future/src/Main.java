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

import java.io.File;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;

public class Main {
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    init();
    appendToBootClassLoader(DEX_EXTRA, /* isCorePlatform */ false);

    Class<?> klass = Object.class.getClassLoader().loadClass("MyClass");
    Method m = klass.getDeclaredMethod("futureHidden");
    Integer result = (Integer) m.invoke(null);
    if (result.intValue() != 42) {
      throw new Error("Expected 42, got " + result.intValue());
    }
  }

  private static final String DEX_EXTRA = new File(System.getenv("DEX_LOCATION"),
      "822-hiddenapi-future-ex.jar").getAbsolutePath();

  private static native void init();
  private static native void appendToBootClassLoader(String dexPath, boolean isCorePlatform);
}
