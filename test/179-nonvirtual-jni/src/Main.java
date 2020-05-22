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
  public static class Base {
    public void sayHi() {
      System.out.println("Hello from Base");
    }
  }
  public static class Ext extends Base{
    public void sayHi() {
      System.out.println("Hello from Ext");
    }
  }
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    try {
      System.out.println("Call lookup: Base, caller: Base, Obj: Base");
      callSayHiMethodNonvirtualWith(Base.class, Base.class, new Base());
    } catch (Exception e) {
      System.out.println("Caught exception " + e);
    }
    try {
      System.out.println("Call lookup: Base, caller: Base, Obj: Ext");
      callSayHiMethodNonvirtualWith(Base.class, Base.class, new Ext());
    } catch (Exception e) {
      System.out.println("Caught exception " + e);
    }
    try {
      System.out.println("Call lookup: Base, caller: Ext, Obj: Ext");
      callSayHiMethodNonvirtualWith(Base.class, Ext.class, new Ext());
    } catch (Exception e) {
      System.out.println("Caught exception " + e);
    }
    try {
      System.out.println("Call lookup: Ext, caller: Ext, Obj: Ext");
      callSayHiMethodNonvirtualWith(Ext.class, Ext.class, new Ext());
    } catch (Exception e) {
      System.out.println("Caught exception " + e);
    }
  }

  private static native void callSayHiMethodNonvirtualWith(Class<?> lookup, Class<?> caller, Object recv);
}
