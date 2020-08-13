/*
 * Copyright (C) 2018 The Android Open Source Project
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

import java.lang.reflect.InvocationTargetException;

public class Linking {
  public static boolean canAccess(String className, boolean takesParameter) throws Exception {
    try {
      Class<?> c = Class.forName(className);
      if (takesParameter) {
        c.getDeclaredMethod("access", Integer.TYPE).invoke(null, 42);
      } else {
        c.getDeclaredMethod("access").invoke(null);
      }
      return true;
    } catch (InvocationTargetException ex) {
      if (ex.getCause() instanceof NoSuchFieldError || ex.getCause() instanceof NoSuchMethodError) {
        return false;
      } else {
        throw ex;
      }
    }
  }
}

// INSTANCE FIELD GET

class LinkFieldGetSdk {
  public static int access() {
    return new ParentClass().fieldPublicSdk;
  }
}

class LinkFieldGetUnsupported {
  public static int access() {
    return new ParentClass().fieldPublicUnsupported;
  }
}

class LinkFieldGetConditionallyBlocked {
  public static int access() {
    return new ParentClass().fieldPublicConditionallyBlocked;
  }
}

class LinkFieldGetBlocklist {
  public static int access() {
    return new ParentClass().fieldPublicBlocklist;
  }
}

class LinkFieldGetBlocklistAndCorePlatformApi {
  public static int access() {
    return new ParentClass().fieldPublicBlocklistAndCorePlatformApi;
  }
}

// INSTANCE FIELD SET

class LinkFieldSetSdk {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    new ParentClass().fieldPublicSdkB = x;
  }
}

class LinkFieldSetUnsupported {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    new ParentClass().fieldPublicUnsupportedB = x;
  }
}

class LinkFieldSetConditionallyBlocked {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    new ParentClass().fieldPublicConditionallyBlockedB = x;
  }
}

class LinkFieldSetBlocklist {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    new ParentClass().fieldPublicBlocklistB = x;
  }
}

class LinkFieldSetBlocklistAndCorePlatformApi {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    new ParentClass().fieldPublicBlocklistAndCorePlatformApiB = x;
  }
}

// STATIC FIELD GET

class LinkFieldGetStaticSdk {
  public static int access() {
    return ParentClass.fieldPublicStaticSdk;
  }
}

class LinkFieldGetStaticUnsupported {
  public static int access() {
    return ParentClass.fieldPublicStaticUnsupported;
  }
}

class LinkFieldGetStaticConditionallyBlocked {
  public static int access() {
    return ParentClass.fieldPublicStaticConditionallyBlocked;
  }
}

class LinkFieldGetStaticBlocklist {
  public static int access() {
    return ParentClass.fieldPublicStaticBlocklist;
  }
}

class LinkFieldGetStaticBlocklistAndCorePlatformApi {
  public static int access() {
    return ParentClass.fieldPublicStaticBlocklistAndCorePlatformApi;
  }
}

// STATIC FIELD SET

class LinkFieldSetStaticSdk {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    ParentClass.fieldPublicStaticSdkB = x;
  }
}

class LinkFieldSetStaticUnsupported {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    ParentClass.fieldPublicStaticUnsupportedB = x;
  }
}

class LinkFieldSetStaticConditionallyBlocked {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    ParentClass.fieldPublicStaticConditionallyBlockedB = x;
  }
}

class LinkFieldSetStaticBlocklist {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    ParentClass.fieldPublicStaticBlocklistB = x;
  }
}

class LinkFieldSetStaticBlocklistAndCorePlatformApi {
  public static void access(int x) {
    // Need to use a different field from the getter to bypass DexCache.
    ParentClass.fieldPublicStaticBlocklistAndCorePlatformApiB = x;
  }
}

// INVOKE INSTANCE METHOD

class LinkMethodSdk {
  public static int access() {
    return new ParentClass().methodPublicSdk();
  }
}

class LinkMethodUnsupported {
  public static int access() {
    return new ParentClass().methodPublicUnsupported();
  }
}

class LinkMethodConditionallyBlocked {
  public static int access() {
    return new ParentClass().methodPublicConditionallyBlocked();
  }
}

class LinkMethodBlocklist {
  public static int access() {
    return new ParentClass().methodPublicBlocklist();
  }
}

class LinkMethodBlocklistAndCorePlatformApi {
  public static int access() {
    return new ParentClass().methodPublicBlocklistAndCorePlatformApi();
  }
}

// INVOKE INSTANCE INTERFACE METHOD

class LinkMethodInterfaceSdk {
  public static int access() {
    return SampleClass.getInterfaceInstance().methodPublicSdk();
  }
}

class LinkMethodInterfaceUnsupported {
  public static int access() {
    return SampleClass.getInterfaceInstance().methodPublicUnsupported();
  }
}

class LinkMethodInterfaceConditionallyBlocked {
  public static int access() {
    return SampleClass.getInterfaceInstance().methodPublicConditionallyBlocked();
  }
}

class LinkMethodInterfaceBlocklist {
  public static int access() {
    return SampleClass.getInterfaceInstance().methodPublicBlocklist();
  }
}

class LinkMethodInterfaceBlocklistAndCorePlatformApi {
  public static int access() {
    return SampleClass.getInterfaceInstance().methodPublicBlocklistAndCorePlatformApi();
  }
}

// INVOKE STATIC METHOD

class LinkMethodStaticSdk {
  public static int access() {
    return ParentClass.methodPublicStaticSdk();
  }
}

class LinkMethodStaticUnsupported {
  public static int access() {
    return ParentClass.methodPublicStaticUnsupported();
  }
}

class LinkMethodStaticConditionallyBlocked {
  public static int access() {
    return ParentClass.methodPublicStaticConditionallyBlocked();
  }
}

class LinkMethodStaticBlocklist {
  public static int access() {
    return ParentClass.methodPublicStaticBlocklist();
  }
}

class LinkMethodStaticBlocklistAndCorePlatformApi {
  public static int access() {
    return ParentClass.methodPublicStaticBlocklistAndCorePlatformApi();
  }
}

// INVOKE INTERFACE STATIC METHOD

class LinkMethodInterfaceStaticSdk {
  public static int access() {
    return ParentInterface.methodPublicStaticSdk();
  }
}

class LinkMethodInterfaceStaticUnsupported {
  public static int access() {
    return ParentInterface.methodPublicStaticUnsupported();
  }
}

class LinkMethodInterfaceStaticConditionallyBlocked {
  public static int access() {
    return ParentInterface.methodPublicStaticConditionallyBlocked();
  }
}

class LinkMethodInterfaceStaticBlocklist {
  public static int access() {
    return ParentInterface.methodPublicStaticBlocklist();
  }
}

class LinkMethodInterfaceStaticBlocklistAndCorePlatformApi {
  public static int access() {
    return ParentInterface.methodPublicStaticBlocklistAndCorePlatformApi();
  }
}
