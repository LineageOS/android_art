/*
 * Copyright (C) 2016 The Android Open Source Project
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

package art;

import java.lang.ref.*;
import java.lang.reflect.*;
import java.lang.invoke.*;
import java.util.*;

public class Test1983 {
  public static void runNonCts() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
    doTestNonCts();
  }
  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static void Check(Class[] klasses) {
    for (Class k : klasses) {
      try {
        boolean res = Redefinition.isStructurallyModifiable(k);
        System.out.println("Is Structurally modifiable " + k + " " + res);
      } catch (Exception e) {
        System.out.println("Got exception " + e + " during check modifiablity of " + k);
        e.printStackTrace(System.out);
      }
    }
  }

  public static class WithVirtuals {
    public Object o;
    public void foobar() {}
  }
  public static class NoVirtuals extends WithVirtuals {
    public static Object o;
    public static void foo() {}
  }
  public static class SubWithVirtuals extends NoVirtuals {
    public Object j;
    public void bar() {}
  }

  public static void doTest() throws Exception {
    Class[] mirrord_classes = new Class[] {
      AccessibleObject.class,
      CallSite.class,
      // ClassExt is not on the compile classpath.
      Class.forName("dalvik.system.ClassExt"),
      ClassLoader.class,
      Class.class,
      Constructor.class,
      // DexCache is not on the compile classpath
      Class.forName("java.lang.DexCache"),
      // EmulatedStackFrame is not on the compile classpath
      Class.forName("dalvik.system.EmulatedStackFrame"),
      Executable.class,
      Field.class,
      // @hide on CTS
      Class.forName("java.lang.ref.FinalizerReference"),
      MethodHandle.class,
      MethodHandles.Lookup.class,
      MethodType.class,
      Method.class,
      Object.class,
      Proxy.class,
      Reference.class,
      StackTraceElement.class,
      String.class,
      Thread.class,
      Throwable.class,
      // @hide on CTS
      Class.forName("java.lang.invoke.VarHandle"),
      // TODO all the var handle types.
      // @hide on CTS
      Class.forName("java.lang.invoke.FieldVarHandle"),
    };
    System.out.println("Checking mirror'd classes");
    Check(mirrord_classes);
    // The results of some of these will change as we improve structural class redefinition. Any
    // that are true should always remain so though.
    Class[] non_mirrord_classes = new Class[] {
      new Object[0].getClass(),
      NoVirtuals.class,
      WithVirtuals.class,
      SubWithVirtuals.class,
    };
    System.out.println("Checking non-mirror'd classes");
    Check(non_mirrord_classes);
  }

  public static void doTestNonCts() throws Exception {
    System.out.println("Checking non-mirror'd classes (non-cts)");
    Class[] non_mirrord_classes = new Class[] {
      ArrayList.class,
      Objects.class,
      Arrays.class,
      Integer.class,
      Number.class,
      MethodHandles.class,
    };
    Check(non_mirrord_classes);
  }
}
