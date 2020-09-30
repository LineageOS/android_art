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

import getters.GetUnresolvedPublicClass;
import getters.GetUnresolvedPublicClassFromDifferentDexFile;
import resolved.PackagePrivateSubclassOfUnresolvedClass;
import resolved.PublicSubclassOfUnresolvedClass;
import resolved.ResolvedPackagePrivateClass;
import resolved.ResolvedPublicSubclassOfPackagePrivateClass;

public class UnresolvedPublicClass {
  public static void $noinline$main() {
    $noinline$testReferrersClass();
    $noinline$testInlinedReferrersClass();
    $noinline$testInlinedReferrersClassFromDifferentDexFile();
    $noinline$testInlinedClassDescriptorCompare1();
    $noinline$testInlinedClassDescriptorCompare2();

    $noinline$testResolvedPublicClass();
    $noinline$testResolvedPackagePrivateClass();
    $noinline$testUnresolvedPublicClass();
    $noinline$testUnresolvedPackagePrivateClass();
    $noinline$testUnresolvedPublicClassInSamePackage();
    $noinline$testUnresolvedPackagePrivateClassInSamePackage();

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

    System.out.println("UnresolvedPublicClass passed");
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testReferrersClass() builder (after)
  /// CHECK: LoadClass class_name:unresolved.UnresolvedPublicClass needs_access_check:false
  static void $noinline$testReferrersClass() {
    Class<?> c = UnresolvedPublicClass.class;
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testInlinedReferrersClass() inliner (after)
  // CHECK: LoadClass class_name:unresolved.UnresolvedPublicClass needs_access_check:false
  static void $noinline$testInlinedReferrersClass() {
    // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
    Class<?> c = GetUnresolvedPublicClass.get();
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testInlinedReferrersClassFromDifferentDexFile() inliner (after)
  // CHECK: LoadClass class_name:unresolved.UnresolvedPublicClass needs_access_check:false
  static void $noinline$testInlinedReferrersClassFromDifferentDexFile() {
    // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
    Class<?> c = GetUnresolvedPublicClassFromDifferentDexFile.get();
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testInlinedClassDescriptorCompare1() inliner (after)
  // CHECK: LoadClass class_name:resolved.PublicSubclassOfUnresolvedClass needs_access_check:true
  static void $noinline$testInlinedClassDescriptorCompare1() {
    // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
    Class<?> c =
        GetUnresolvedPublicClassFromDifferentDexFile.getOtherClass();
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testInlinedClassDescriptorCompare2() inliner (after)
  // CHECK: LoadClass class_name:unresolved.UnresolvedPublicClazz needs_access_check:true
  static void $noinline$testInlinedClassDescriptorCompare2() {
    // This is useful for code coverage of descriptor comparison
    // implemented by first comparing the utf16 lengths and then
    // checking strcmp(). Using these classes we cover the path
    // where utf16 lengths match but string contents differ.
    // TODO: Make $inline$ and enable CHECK above when we relax the verifier. b/28313047
    Class<?> c =
        GetUnresolvedPublicClassFromDifferentDexFile.getOtherClassWithSameDescriptorLength();
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testResolvedPublicClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.ResolvedPublicSubclassOfPackagePrivateClass needs_access_check:false
  static void $noinline$testResolvedPublicClass() {
    Class<?> c = ResolvedPublicSubclassOfPackagePrivateClass.class;
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testResolvedPackagePrivateClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.ResolvedPackagePrivateClass needs_access_check:true
  static void $noinline$testResolvedPackagePrivateClass() {
    try {
      Class<?> c = ResolvedPackagePrivateClass.class;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testUnresolvedPublicClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.PublicSubclassOfUnresolvedClass needs_access_check:true

  /// CHECK-START-{ARM,ARM64,X86,X86_64}: void unresolved.UnresolvedPublicClass.$noinline$testUnresolvedPublicClass() builder (after)
  /// CHECK: LoadClass load_kind:BssEntryPublic class_name:resolved.PublicSubclassOfUnresolvedClass
  static void $noinline$testUnresolvedPublicClass() {
    Class<?> c = PublicSubclassOfUnresolvedClass.class;
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testUnresolvedPackagePrivateClass() builder (after)
  /// CHECK: LoadClass class_name:resolved.PackagePrivateSubclassOfUnresolvedClass needs_access_check:true

  /// CHECK-START-{ARM,ARM64,X86,X86_64}: void unresolved.UnresolvedPublicClass.$noinline$testUnresolvedPackagePrivateClass() builder (after)
  /// CHECK: LoadClass load_kind:BssEntryPublic class_name:resolved.PackagePrivateSubclassOfUnresolvedClass
  static void $noinline$testUnresolvedPackagePrivateClass() {
    try {
      Class<?> c = PackagePrivateSubclassOfUnresolvedClass.class;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testUnresolvedPublicClassInSamePackage() builder (after)
  /// CHECK: LoadClass class_name:unresolved.UnresolvedPublicClazz needs_access_check:true

  /// CHECK-START-{ARM,ARM64,X86,X86_64}: void unresolved.UnresolvedPublicClass.$noinline$testUnresolvedPublicClassInSamePackage() builder (after)
  /// CHECK: LoadClass load_kind:BssEntryPackage class_name:unresolved.UnresolvedPublicClazz
  static void $noinline$testUnresolvedPublicClassInSamePackage() {
    Class<?> c = UnresolvedPublicClazz.class;
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testUnresolvedPackagePrivateClassInSamePackage() builder (after)
  /// CHECK: LoadClass class_name:unresolved.UnresolvedPackagePrivateClass needs_access_check:true

  /// CHECK-START-{ARM,ARM64,X86,X86_64}: void unresolved.UnresolvedPublicClass.$noinline$testUnresolvedPackagePrivateClassInSamePackage() builder (after)
  /// CHECK: LoadClass load_kind:BssEntryPackage class_name:unresolved.UnresolvedPackagePrivateClass
  static void $noinline$testUnresolvedPackagePrivateClassInSamePackage() {
    Class<?> c = UnresolvedPackagePrivateClass.class;
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPublicFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPublicFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPublicFieldInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.publicIntField = 42;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPublicFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: StaticFieldSet

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPublicFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: UnresolvedStaticFieldSet
  static void $noinline$testPublicFieldInPackagePrivateClassViaResolvedPublicSubclass() {
    ResolvedPublicSubclassOfPackagePrivateClass.publicIntField = 42;
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPrivateFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPrivateFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPrivateFieldInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.privateIntField = 42;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() {
    try {
      ResolvedPublicSubclassOfPackagePrivateClass.privateIntField = 42;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPackagePrivateFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPackagePrivateFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPackagePrivateFieldInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.intField = 42;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPackagePrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPackagePrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPackagePrivateFieldInPackagePrivateClassViaResolvedPublicSubclass() {
    try {
      ResolvedPublicSubclassOfPackagePrivateClass.intField = 42;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPublicMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK: InvokeUnresolved method_name:{{[^$]*}}$noinline$publicStaticMethod

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPublicMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$publicStaticMethod
  static void $noinline$testPublicMethodInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.$noinline$publicStaticMethod();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPublicMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$publicStaticMethod

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPublicMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: InvokeUnresolved method_name:{{[^$]*}}$noinline$publicStaticMethod
  static void $noinline$testPublicMethodInPackagePrivateClassViaResolvedPublicSubclass() {
    ResolvedPublicSubclassOfPackagePrivateClass.$noinline$publicStaticMethod();
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPrivateMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK: InvokeUnresolved method_name:{{[^$]*}}$noinline$privateStaticMethod

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPrivateMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$privateStaticMethod
  static void $noinline$testPrivateMethodInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.$noinline$privateStaticMethod();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: InvokeUnresolved method_name:{{[^$]*}}$noinline$privateStaticMethod

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$privateStaticMethod
  static void $noinline$testPrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() {
    try {
      ResolvedPublicSubclassOfPackagePrivateClass.$noinline$privateStaticMethod();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPackagePrivateMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK: InvokeUnresolved method_name:{{[^$]*}}$noinline$staticMethod

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPackagePrivateMethodInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$staticMethod
  static void $noinline$testPackagePrivateMethodInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.$noinline$staticMethod();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPackagePrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK: InvokeUnresolved method_name:{{[^$]*}}$noinline$staticMethod

  /// CHECK-START: void unresolved.UnresolvedPublicClass.$noinline$testPackagePrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: InvokeStaticOrDirect method_name:{{[^$]*}}$noinline$staticMethod
  static void $noinline$testPackagePrivateMethodInPackagePrivateClassViaResolvedPublicSubclass() {
    try {
      ResolvedPublicSubclassOfPackagePrivateClass.$noinline$staticMethod();
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }
}
