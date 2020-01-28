/*
 * Copyright (C) 2017 The Android Open Source Project
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

import art.*;

public class Main {
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

  public static class C1 {
    public Object o;
    public void foobar() {}
  }
  public static class C2 extends C1 {
    public static Object o;
    public static void foo() {}
  }
  public static class C3 extends C2 {
    public Object j;
    public void bar() {}
  }

  public static void doTest() throws Exception {
    Class[] classes = new Class[] {
      C1.class,
      C2.class,
      C3.class,
    };
    System.out.println("Checking classes");
    Check(classes);
    System.out.println("Setting C2 as having pointer-ids used and checking classes");
    SetPointerIdsUsed(C2.class);
    Check(classes);
  }
  public static native void SetPointerIdsUsed(Class<?> k);
  public static void main(String[] args) throws Exception {
    // Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }
}
