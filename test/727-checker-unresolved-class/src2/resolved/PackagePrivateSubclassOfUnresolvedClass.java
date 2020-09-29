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

package resolved;

import getters.GetPackagePrivateSubclassOfUnresolvedClass;
import unresolved.UnresolvedPublicClass;

class PackagePrivateSubclassOfUnresolvedClass extends UnresolvedPublicClass {
  public static void $noinline$main() {
    $noinline$testReferrersClass();
    $noinline$testInlinedReferrersClass();
    $noinline$testInlinedReferrersClassFromSamePackage();

    System.out.println("PackagePrivateSubclassOfUnresolvedClass passed");
  }

  /// CHECK-START: void resolved.PackagePrivateSubclassOfUnresolvedClass.$noinline$testReferrersClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.PackagePrivateSubclassOfUnresolvedClass needs_access_check:false
  static void $noinline$testReferrersClass() {
    Class<?> c = PackagePrivateSubclassOfUnresolvedClass.class;
  }

  /// CHECK-START: void resolved.PackagePrivateSubclassOfUnresolvedClass.$noinline$testInlinedReferrersClass() inliner (after)
  // CHECK: LoadClass class_name:resolved.PackagePrivateSubclassOfUnresolvedClass needs_access_check:true
  static void $noinline$testInlinedReferrersClass() {
    try {
      // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
      Class<?> c = GetPackagePrivateSubclassOfUnresolvedClass.get();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void resolved.PackagePrivateSubclassOfUnresolvedClass.$noinline$testInlinedReferrersClassFromSamePackage() inliner (after)
  // CHECK: LoadClass class_name:resolved.PackagePrivateSubclassOfUnresolvedClass needs_access_check:false
  static void $noinline$testInlinedReferrersClassFromSamePackage() {
    // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
    Class<?> c = GetPackagePrivateSubclassOfUnresolvedClassFromSamePackage.get();
  }
}
