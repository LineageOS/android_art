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

package unresolved;

import getters.GetUnresolvedPackagePrivateClass;

class UnresolvedPackagePrivateClass {
  public static void $noinline$main() {
    $noinline$testReferrersClass();
    $noinline$testInlinedReferrersClass();

    System.out.println("UnresolvedPackagePrivateClass passed");
  }

  /// CHECK-START: void unresolved.UnresolvedPackagePrivateClass.$noinline$testReferrersClass() builder (after)
  /// CHECK: LoadClass class_name:unresolved.UnresolvedPackagePrivateClass needs_access_check:false
  static void $noinline$testReferrersClass() {
    Class<?> c = UnresolvedPackagePrivateClass.class;
  }

  /// CHECK-START: void unresolved.UnresolvedPackagePrivateClass.$noinline$testInlinedReferrersClass() inliner (after)
  // CHECK: LoadClass class_name:unresolved.UnresolvedPackagePrivateClass needs_access_check:true
  static void $noinline$testInlinedReferrersClass() {
    try {
      // TODO: When we relax verifier to ignore access check failures,
      // change the called method to `$inline$` and enable the CHECK above.
      Class<?> c = GetUnresolvedPackagePrivateClass.get();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }
}
