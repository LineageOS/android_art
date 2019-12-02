/*
 * Copyright (C) 2019 The Android Open Source Project
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

import java.util.function.Consumer;

public class Main {
  public static final boolean PRINT = false;

  public static class PtrCls {
    public static void doNothingPtr() {}
  }

  public static class IdxCls {
    public static void doNothingIdx() {}
  }

  public static void DbgPrint(String str) {
    if (PRINT) {
      System.out.println(str);
    }
  }

  public static long GetId(Class<?> k, String name) {
    return GetMethodId(true, k, name, "()V");
  }

  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    System.out.println("JNI Type is: " + GetJniType());
    long expect_ptr_id = GetId(PtrCls.class, "doNothingPtr");
    DbgPrint(String.format("expected_ptr_id is 0x%x", expect_ptr_id));
    if (expect_ptr_id % 4 != 0) {
      throw new Error("ID " + expect_ptr_id + " is not aligned!");
    } else {
      System.out.println("pointer ID looks like a pointer!");
    }
    SetToIndexIds();
    System.out.println("JNI Type is: " + GetJniType());
    long expect_idx_id = GetId(IdxCls.class, "doNothingIdx");
    DbgPrint(String.format("expected_idx_id is 0x%x", expect_idx_id));
    if (expect_idx_id % 2 != 1) {
      throw new Error("ID " + expect_ptr_id + " is not odd!");
    } else {
      System.out.println("index ID looks like an index!");
    }
    long again_ptr_id = GetId(PtrCls.class, "doNothingPtr");
    if (expect_ptr_id != again_ptr_id) {
      throw new Error(
          "Got different id values for same method. " + expect_ptr_id + " vs " + again_ptr_id);
    } else {
      System.out.println("pointer ID remains a pointer!");
    }
    long well_known_id = GetMethodId(false, Consumer.class, "accept", "(Ljava/lang/Object;)V");
    DbgPrint(String.format("well_known_id is 0x%x", well_known_id));
    if (well_known_id % 2 != 1) {
      throw new Error("WKC ID " + well_known_id + " is not odd!");
    } else {
      System.out.println("index WKC ID looks like an index!");
    }
  }

  private static native String GetJniType();
  private static native void SetToIndexIds();
  private static native long GetMethodId(boolean is_static, Class k, String name, String sig);
}
