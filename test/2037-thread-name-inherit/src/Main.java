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

import java.util.function.*;

public class Main {
  public static final boolean IS_ART = System.getProperty("java.vm.name").equals("Dalvik");

  public static final class Names {
    public final String native_name;
    public final String java_name;

    public Names(String ntv, String java) {
      this.native_name = ntv;
      this.java_name = java;
    }

    public boolean equals(Object o) {
      if (o instanceof Names) {
        Names on = (Names) o;
        return on.native_name.equals(native_name) && on.java_name.equals(java_name);
      } else {
        return false;
      }
    }

    public String toString() {
      return "Names{native: \"" + native_name + "\", java: \"" + java_name + "\"}";
    }
  }

  public static void checkDefaultNames(Names res) {
    if (IS_ART) {
      if (!res.native_name.matches("Thread-[0-9]+")) {
        throw new Error("Bad thread name! " + res);
      }
    } else {
      if (!res.native_name.equals("native-thread")) {
        throw new Error("Bad thread name! " + res);
      }
    }
    if (!res.java_name.matches("Thread-[0-9]+")) {
      throw new Error("Bad thread name! " + res);
    }
  }

  public static void checkNames(Names res, Names art_exp, Names ri_exp) {
    if (IS_ART) {
      if (!res.equals(art_exp)) {
        throw new Error("Not equal " + res + " != " + art_exp);
      }
    } else {
      if (!res.equals(ri_exp)) {
        throw new Error("Not equal " + res + " != " + ri_exp);
      }
    }
  }

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    Names[] name = new Names[1];
    BiConsumer<String, Thread> thdResult =
        (String native_name, Thread jthread) -> {
          name[0] = new Names(native_name, jthread.getName());
        };

    runThreadTest(thdResult);
    checkDefaultNames(name[0]);

    runThreadTestWithName(thdResult);
    checkNames(
        name[0],
        new Names("java-native-thr", "java-native-thread"),
        new Names("native-thread", "java-native-thread"));

    runThreadTestSetJava(thdResult);
    checkNames(
        name[0],
        new Names("native-thread-s", "native-thread-set-java"),
        new Names("native-thread", "native-thread-set-java"));
  }

  public static native void runThreadTest(BiConsumer<String, Thread> results);

  public static native void runThreadTestWithName(BiConsumer<String, Thread> results);

  public static native void runThreadTestSetJava(BiConsumer<String, Thread> results);
}
