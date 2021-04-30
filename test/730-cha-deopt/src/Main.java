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

interface Itf {
  public String interfaceMethod();
}

public class Main implements Itf {

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    ensureJitCompiled(Main.class, "$noinline$invoke");
    String result = $noinline$invoke(new Main());
    if (!"Main".equals(result)) {
      throw new Error("Expected Main, got " + result);
    }

    // Load SubItf dynamically, to avoid prior verification of this method to load it.
    Class<?> cls = Class.forName("SubItf");
    Itf itf = (Itf) cls.newInstance();

    result = $noinline$invoke(itf);
    if (!"SubItf".equals(result)) {
      throw new Error("Expected SubItf, got " + result);
    }

  }

  public static String $noinline$invoke(Itf itf) {
    return itf.interfaceMethod();
  }

  public String interfaceMethod() {
    return "Main";
  }

  public static native void ensureJitCompiled(Class<?> cls, String methodName);
}

class SubItf implements Itf {
  public String interfaceMethod() {
    return "SubItf";
  }
}
