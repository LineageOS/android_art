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

import resolved.ResolvedPackagePrivateClass;
import resolved.ResolvedPublicSubclassOfPackagePrivateClass;

public class UnresolvedClass {
  public static void $noinline$main() {
    $noinline$testPublicFieldInResolvedPackagePrivateClass();
    $noinline$testPublicFieldInPackagePrivateClassReferencedViaResolvedPublicSubclass();
  }

  /// CHECK-START: void unresolved.UnresolvedClass.$noinline$testPublicFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void unresolved.UnresolvedClass.$noinline$testPublicFieldInResolvedPackagePrivateClass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPublicFieldInResolvedPackagePrivateClass() {
    try {
      ResolvedPackagePrivateClass.publicIntField = 42;
      throw new Error("Unreachable");
    } catch (IllegalAccessError expected) {}
  }

  /// CHECK-START: void unresolved.UnresolvedClass.$noinline$testPublicFieldInPackagePrivateClassReferencedViaResolvedPublicSubclass() builder (after)
  /// CHECK: UnresolvedStaticFieldSet

  /// CHECK-START: void unresolved.UnresolvedClass.$noinline$testPublicFieldInPackagePrivateClassReferencedViaResolvedPublicSubclass() builder (after)
  /// CHECK-NOT: StaticFieldSet
  static void $noinline$testPublicFieldInPackagePrivateClassReferencedViaResolvedPublicSubclass() {
    // TODO: Use StaticFieldSet when the referenced class is public.
    ResolvedPublicSubclassOfPackagePrivateClass.publicIntField = 42;
  }
}
