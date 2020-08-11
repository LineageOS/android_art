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

public interface ParentInterface {
  // STATIC FIELD
  static int fieldPublicStaticSdk = 11;
  static int fieldPublicStaticUnsupported = 12;
  static int fieldPublicStaticConditionallyBlocked = 13;
  static int fieldPublicStaticBlocklist = 14;
  static int fieldPublicStaticBlocklistAndCorePlatformApi = 15;

  // INSTANCE METHOD
  int methodPublicSdk();
  int methodPublicUnsupported();
  int methodPublicConditionallyBlocked();
  int methodPublicBlocklist();
  int methodPublicBlocklistAndCorePlatformApi();

  // STATIC METHOD
  static int methodPublicStaticSdk() { return 21; }
  static int methodPublicStaticUnsupported() { return 22; }
  static int methodPublicStaticConditionallyBlocked() { return 23; }
  static int methodPublicStaticBlocklist() { return 24; }
  static int methodPublicStaticBlocklistAndCorePlatformApi() { return 25; }

  // DEFAULT METHOD
  default int methodPublicDefaultSdk() { return 31; }
  default int methodPublicDefaultUnsupported() { return 32; }
  default int methodPublicDefaultConditionallyBlocked() { return 33; }
  default int methodPublicDefaultBlocklist() { return 34; }
  default int methodPublicDefaultBlocklistAndCorePlatformApi() { return 35; }
}
