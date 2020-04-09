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

import dalvik.system.PathClassLoader;

class Main {
  static final String DEX_FILE = System.getenv("DEX_LOCATION")
      + "/726-load-duplicate-dex-files.jar";
  static final String DEX_FILES = DEX_FILE + ":" + DEX_FILE;

  public static void main(String[] args) throws Exception {
    // Adding duplicate dex files to the classpath will trigger a runtime warning.
    PathClassLoader p = new PathClassLoader(DEX_FILES, Main.class.getClassLoader());
  }
}
