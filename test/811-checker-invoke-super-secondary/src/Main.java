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

import dalvik.system.PathClassLoader;

class Main {
  static final String TEST_NAME = "811-checker-invoke-super-secondary";

  static final String DEX_FILE = System.getenv("DEX_LOCATION") + "/" + TEST_NAME + ".jar";

  static final String SECONDARY_NAME = TEST_NAME + "-ex";
  static final String SECONDARY_DEX_FILE =
    System.getenv("DEX_LOCATION") + "/" + SECONDARY_NAME + ".jar";

  public static void main(String[] args) throws Exception {
    PathClassLoader pcl = new PathClassLoader(SECONDARY_DEX_FILE, Main.class.getClassLoader());

    Class<?> secondaryCls = pcl.loadClass("OtherClass");
    secondaryCls.getDeclaredMethod("$noinline$foo").invoke(secondaryCls.newInstance());
  }
}
