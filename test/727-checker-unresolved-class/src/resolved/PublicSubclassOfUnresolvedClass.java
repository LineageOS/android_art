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

import getters.GetPublicSubclassOfUnresolvedClass;
import unresolved.UnresolvedPublicClass;

public class PublicSubclassOfUnresolvedClass extends UnresolvedPublicClass {
  public static void $noinline$main() {
    $noinline$testReferrersClass();
    $noinline$testInlinedReferrersClass();
    $noinline$testInlinedReferrersClassFromSamePackage();

    $noinline$testResolvedPublicClass();
    $noinline$testResolvedPackagePrivateClass();

    $noinline$testPublicFieldInResolvedPackagePrivateClass();
    $noinline$testPublicFieldInPackagePrivateClassViaResolvedPublicSubclass();
    $noinline$testPrivateFieldInResolvedPackagePrivateClass();
    $noinline$testPrivateFieldInPackagePrivateClassViaResolvedPublicSubclass();
    $noinline$testPackagePrivateFieldInResolvedPackagePrivateClass();
    $noinline$testPackagePrivateFieldInPackagePrivateClassViaResolvedPublicSubclass();

    $noinline$testPublicMethodInResolvedPackagePrivateClass();
    $noinline$testPublicMethodInPackagePrivateClassViaResolvedPublicSubclass();
    $noinline$testPrivateMethodInResolvedPackagePrivateClass();
    $noinline$testPrivateMethodInPackagePrivateClassViaResolvedPublicSubclass();
    $noinline$testPackagePrivateMethodInResolvedPackagePrivateClass();
    $noinline$testPackagePrivateMethodInPackagePrivateClassViaResolvedPublicSubclass();

    System.out.println("PublicSubclassOfUnresolvedClass passed");
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testReferrersClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.PublicSubclassOfUnresolvedClass needs_access_check:false
  static void $noinline$testReferrersClass() {
    Class<?> c = PublicSubclassOfUnresolvedClass.class;
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testInlinedReferrersClass() inliner (after)
  // CHECK: LoadClass class_name:resolved.PublicSubclassOfUnresolvedClass needs_access_check:false
  static void $noinline$testInlinedReferrersClass() {
    // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
    Class<?> c = GetPublicSubclassOfUnresolvedClass.get();
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testInlinedReferrersClassFromSamePackage() inliner (after)
  // CHECK: LoadClass class_name:resolved.PublicSubclassOfUnresolvedClass needs_access_check:false
  static void $noinline$testInlinedReferrersClassFromSamePackage() {
    // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
    Class<?> c = GetPublicSubclassOfUnresolvedClassFromSamePackage.get();
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testResolvedPublicClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.ResolvedPublicSubclassOfPackagePrivateClass needs_access_check:false
  static void $noinline$testResolvedPublicClass() {
    Class<?> c = ResolvedPublicSubclassOfPackagePrivateClass.class;
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testResolvedPackagePrivateClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.ResolvedPackagePrivateClass needs_access_check:false
  static void $noinline$testResolvedPackagePrivateClass() {
    Class<?> c = ResolvedPackagePrivateClass.class;
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPublicFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK: StaticFieldSet

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPublicFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: UnresolvedStaticFieldSet
  static void $noinline$testPublicFieldInResolvedPackagePrivateClass() {
    ResolvedPackagePrivateClass.publicIntField = 42;
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPublicFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: StaticFieldSet

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPublicFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: UnresolvedStaticFieldSet
  static void $noinline$testPublicFieldInPackagePrivateClassViaResolvedPublicSubclass() {
    ResolvedPublicSubclassOfPackagePrivateClass.publicIntField = 42;
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPrivateFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPrivateFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPrivateFieldInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.privateIntField = 42;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() {
    try {
      ResolvedPublicSubclassOfPackagePrivateClass.privateIntField = 42;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPackagePrivateFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK: StaticFieldSet

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPackagePrivateFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: UnresolvedStaticFieldSet
  static void $noinline$testPackagePrivateFieldInResolvedPackagePrivateClass() {
    ResolvedPackagePrivateClass.intField = 42;
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPackagePrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: StaticFieldSet

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPackagePrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: UnresolvedStaticFieldSet
  static void $noinline$testPackagePrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() {
    ResolvedPublicSubclassOfPackagePrivateClass.intField = 42;
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPublicMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$publicStaticMethod

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPublicMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: InvokeUnresolved method_name:{{[^$]*}}$noinline$publicStaticMethod
  static void $noinline$testPublicMethodInResolvedPackagePrivateClass() {
    ResolvedPackagePrivateClass.$noinline$publicStaticMethod();
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPublicMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$publicStaticMethod

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPublicMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: InvokeUnresolved method_name:{{[^$]*}}$noinline$publicStaticMethod
  static void $noinline$testPublicMethodInPackagePrivateClassViaResolvedPublicSubclass() {
    ResolvedPublicSubclassOfPackagePrivateClass.$noinline$publicStaticMethod();
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPrivateMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK: InvokeUnresolved method_name:{{[^$]*}}$noinline$privateStaticMethod

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPrivateMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$privateStaticMethod
  static void $noinline$testPrivateMethodInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.$noinline$privateStaticMethod();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: InvokeUnresolved method_name:{{[^$]*}}$noinline$privateStaticMethod

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$privateStaticMethod
  static void $noinline$testPrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() {
    try {
      ResolvedPublicSubclassOfPackagePrivateClass.$noinline$privateStaticMethod();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPackagePrivateMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$staticMethod

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPackagePrivateMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: InvokeUnresolved method_name:{{[^$]*}}$noinline$staticMethod
  static void $noinline$testPackagePrivateMethodInResolvedPackagePrivateClass() {
    ResolvedPackagePrivateClass.$noinline$staticMethod();
  }

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPackagePrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$staticMethod

  /// CHECK-START: void resolved.PublicSubclassOfUnresolvedClass.$noinline$testPackagePrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: InvokeUnresolved method_name:{{[^$]*}}$noinline$staticMethod
  static void $noinline$testPackagePrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() {
    ResolvedPublicSubclassOfPackagePrivateClass.$noinline$staticMethod();
  }
}
