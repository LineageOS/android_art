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

// This class is used for compiling code that accesses it but it is
// replaced by a package-private class from src-ex2/ with reduced access
// to run tests, including access check tests.
public class PackagePrivateSubclassOfUnresolvedClass2 extends UnresolvedPublicClass {
  public static void $noinline$main() {
    throw new Error("Unreachable");
  }
}
