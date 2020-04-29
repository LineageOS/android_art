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
  public static void main(String[] args) {
    System.loadLibrary(args[0]);

    if ("speed-profile".equals(getCompilerFilter(Main.class)) && !hasAppImage() && hasOatFile()) {
      System.out.println("Error: Loaded OAT file with no app image in speed-profile mode");
    }

    System.out.println("Test passed.");
  }

  private native static boolean hasAppImage();
  private native static boolean hasOatFile();
  private native static Object getCompilerFilter(Class c);
}
