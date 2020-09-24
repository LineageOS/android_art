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

import getters.GetPackagePrivateSubclassOfUnresolvedClass2;
import unresolved.UnresolvedPublicClass;

// This class is defined by the child class loader, so access to
// package-private classes and members defined in the parent class
// loader is illegal even though the package name is the same.
public class PackagePrivateSubclassOfUnresolvedClass2 extends UnresolvedPublicClass {
  public static void $noinline$main() {
    $noinline$testReferrersClass();
    $noinline$testInlinedReferrersClass();
    $noinline$testInlinedReferrersClassFromSamePackage();

    System.out.println("PackagePrivateSubclassOfUnresolvedClass2 passed");
  }

  /// CHECK-START: void resolved.PackagePrivateSubclassOfUnresolvedClass2.$noinline$testReferrersClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.PackagePrivateSubclassOfUnresolvedClass2 needs_access_check:false
  static void $noinline$testReferrersClass() {
    Class<?> c = PackagePrivateSubclassOfUnresolvedClass2.class;
  }

  /// CHECK-START: void resolved.PackagePrivateSubclassOfUnresolvedClass2.$noinline$testInlinedReferrersClass() inliner (after)
  // CHECK: LoadClass class_name:resolved.PackagePrivateSubclassOfUnresolvedClass2 needs_access_check:true
  static void $noinline$testInlinedReferrersClass() {
    // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
    Class<?> c = GetPackagePrivateSubclassOfUnresolvedClass2.get();
  }

  /// CHECK-START: void resolved.PackagePrivateSubclassOfUnresolvedClass2.$noinline$testInlinedReferrersClassFromSamePackage() inliner (after)
  // CHECK: LoadClass class_name:resolved.PackagePrivateSubclassOfUnresolvedClass2 needs_access_check:true
  static void $noinline$testInlinedReferrersClassFromSamePackage() {
    // Trying to resolve this class by name in parent class loader throws NoClassDefFoundError.
    try {
      // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
      Class<?> c = GetPackagePrivateSubclassOfUnresolvedClass2FromSamePackage.get();
      throw new Error("Unreachable");
    } catch (NoClassDefFoundError expected) {}
  }
}
