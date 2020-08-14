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

public class ParentClass {
  public ParentClass() {}

  // INSTANCE FIELD

  public int fieldPublicSdk = 211;
  int fieldPackageSdk = 212;
  protected int fieldProtectedSdk = 213;
  private int fieldPrivateSdk = 214;
  public int fieldPublicSdkB = 215;

  public int fieldPublicUnsupported = 221;
  int fieldPackageUnsupported = 222;
  protected int fieldProtectedUnsupported = 223;
  private int fieldPrivateUnsupported = 224;
  public int fieldPublicUnsupportedB = 225;

  public int fieldPublicConditionallyBlocked = 231;
  int fieldPackageConditionallyBlocked = 232;
  protected int fieldProtectedConditionallyBlocked = 233;
  private int fieldPrivateConditionallyBlocked = 234;
  public int fieldPublicConditionallyBlockedB = 235;

  public int fieldPublicBlocklist = 241;
  int fieldPackageBlocklist = 242;
  protected int fieldProtectedBlocklist = 243;
  private int fieldPrivateBlocklist = 244;
  public int fieldPublicBlocklistB = 245;

  public int fieldPublicBlocklistAndCorePlatformApi = 251;
  int fieldPackageBlocklistAndCorePlatformApi = 252;
  protected int fieldProtectedBlocklistAndCorePlatformApi = 253;
  private int fieldPrivateBlocklistAndCorePlatformApi = 254;
  public int fieldPublicBlocklistAndCorePlatformApiB = 255;

  // STATIC FIELD

  public static int fieldPublicStaticSdk = 111;
  static int fieldPackageStaticSdk = 112;
  protected static int fieldProtectedStaticSdk = 113;
  private static int fieldPrivateStaticSdk = 114;
  public static int fieldPublicStaticSdkB = 115;

  public static int fieldPublicStaticUnsupported = 121;
  static int fieldPackageStaticUnsupported = 122;
  protected static int fieldProtectedStaticUnsupported = 123;
  private static int fieldPrivateStaticUnsupported = 124;
  public static int fieldPublicStaticUnsupportedB = 125;

  public static int fieldPublicStaticConditionallyBlocked = 131;
  static int fieldPackageStaticConditionallyBlocked = 132;
  protected static int fieldProtectedStaticConditionallyBlocked = 133;
  private static int fieldPrivateStaticConditionallyBlocked = 134;
  public static int fieldPublicStaticConditionallyBlockedB = 135;

  public static int fieldPublicStaticBlocklist = 141;
  static int fieldPackageStaticBlocklist = 142;
  protected static int fieldProtectedStaticBlocklist = 143;
  private static int fieldPrivateStaticBlocklist = 144;
  public static int fieldPublicStaticBlocklistB = 145;

  public static int fieldPublicStaticBlocklistAndCorePlatformApi = 151;
  static int fieldPackageStaticBlocklistAndCorePlatformApi = 152;
  protected static int fieldProtectedStaticBlocklistAndCorePlatformApi = 153;
  private static int fieldPrivateStaticBlocklistAndCorePlatformApi = 154;
  public static int fieldPublicStaticBlocklistAndCorePlatformApiB = 155;

  // INSTANCE METHOD

  public int methodPublicSdk() { return 411; }
  int methodPackageSdk() { return 412; }
  protected int methodProtectedSdk() { return 413; }
  private int methodPrivateSdk() { return 414; }

  public int methodPublicUnsupported() { return 421; }
  int methodPackageUnsupported() { return 422; }
  protected int methodProtectedUnsupported() { return 423; }
  private int methodPrivateUnsupported() { return 424; }

  public int methodPublicConditionallyBlocked() { return 431; }
  int methodPackageConditionallyBlocked() { return 432; }
  protected int methodProtectedConditionallyBlocked() { return 433; }
  private int methodPrivateConditionallyBlocked() { return 434; }

  public int methodPublicBlocklist() { return 441; }
  int methodPackageBlocklist() { return 442; }
  protected int methodProtectedBlocklist() { return 443; }
  private int methodPrivateBlocklist() { return 444; }

  public int methodPublicBlocklistAndCorePlatformApi() { return 451; }
  int methodPackageBlocklistAndCorePlatformApi() { return 452; }
  protected int methodProtectedBlocklistAndCorePlatformApi() { return 453; }
  private int methodPrivateBlocklistAndCorePlatformApi() { return 454; }

  // STATIC METHOD

  public static int methodPublicStaticSdk() { return 311; }
  static int methodPackageStaticSdk() { return 312; }
  protected static int methodProtectedStaticSdk() { return 313; }
  private static int methodPrivateStaticSdk() { return 314; }

  public static int methodPublicStaticUnsupported() { return 321; }
  static int methodPackageStaticUnsupported() { return 322; }
  protected static int methodProtectedStaticUnsupported() { return 323; }
  private static int methodPrivateStaticUnsupported() { return 324; }

  public static int methodPublicStaticConditionallyBlocked() { return 331; }
  static int methodPackageStaticConditionallyBlocked() { return 332; }
  protected static int methodProtectedStaticConditionallyBlocked() { return 333; }
  private static int methodPrivateStaticConditionallyBlocked() { return 334; }

  public static int methodPublicStaticBlocklist() { return 341; }
  static int methodPackageStaticBlocklist() { return 342; }
  protected static int methodProtectedStaticBlocklist() { return 343; }
  private static int methodPrivateStaticBlocklist() { return 344; }

  public static int methodPublicStaticBlocklistAndCorePlatformApi() { return 351; }
  static int methodPackageStaticBlocklistAndCorePlatformApi() { return 352; }
  protected static int methodProtectedStaticBlocklistAndCorePlatformApi() { return 353; }
  private static int methodPrivateStaticBlocklistAndCorePlatformApi() { return 354; }

  // CONSTRUCTOR

  // Sdk
  public ParentClass(int x, short y) {}
  ParentClass(float x, short y) {}
  protected ParentClass(long x, short y) {}
  private ParentClass(double x, short y) {}

  // Light greylist
  public ParentClass(int x, boolean y) {}
  ParentClass(float x, boolean y) {}
  protected ParentClass(long x, boolean y) {}
  private ParentClass(double x, boolean y) {}

  // Dark greylist
  public ParentClass(int x, byte y) {}
  ParentClass(float x, byte y) {}
  protected ParentClass(long x, byte y) {}
  private ParentClass(double x, byte y) {}

  // Blocklist
  public ParentClass(int x, char y) {}
  ParentClass(float x, char y) {}
  protected ParentClass(long x, char y) {}
  private ParentClass(double x, char y) {}

  // Blocklist and CorePlatformApi
  public ParentClass(int x, int y) {}
  ParentClass(float x, int y) {}
  protected ParentClass(long x, int y) {}
  private ParentClass(double x, int y) {}

  // HELPERS

  public int callMethodPublicSdk() { return methodPublicSdk(); }
  public int callMethodPackageSdk() { return methodPackageSdk(); }
  public int callMethodProtectedSdk() { return methodProtectedSdk(); }

  public int callMethodPublicUnsupported() { return methodPublicUnsupported(); }
  public int callMethodPackageUnsupported() { return methodPackageUnsupported(); }
  public int callMethodProtectedUnsupported() { return methodProtectedUnsupported(); }

  public int callMethodPublicConditionallyBlocked() { return methodPublicConditionallyBlocked(); }
  public int callMethodPackageConditionallyBlocked() { return methodPackageConditionallyBlocked(); }
  public int callMethodProtectedConditionallyBlocked() { return methodProtectedConditionallyBlocked(); }

  public int callMethodPublicBlocklist() { return methodPublicBlocklist(); }
  public int callMethodPackageBlocklist() { return methodPackageBlocklist(); }
  public int callMethodProtectedBlocklist() { return methodProtectedBlocklist(); }

  public int callMethodPublicBlocklistAndCorePlatformApi() {
    return methodPublicBlocklistAndCorePlatformApi();
  }

  public int callMethodPackageBlocklistAndCorePlatformApi() {
    return methodPackageBlocklistAndCorePlatformApi();
  }

  public int callMethodProtectedBlocklistAndCorePlatformApi() {
    return methodProtectedBlocklistAndCorePlatformApi();
  }
}
