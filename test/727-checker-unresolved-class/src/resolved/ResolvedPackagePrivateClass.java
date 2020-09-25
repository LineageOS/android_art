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

// This class is used for compiling code that accesses its fields but it is
// replaced by a package-private class from src2/ with reduced access to
// some members to test different access checks.
public class ResolvedPackagePrivateClass {
  public static int publicIntField;
  public static int privateIntField;
  public static int intField;

  public static void $noinline$publicStaticMethod() {
    System.out.println("ResolvedPackagePrivateClass.$noinline$publicStaticMethod()");
  }

  public static void $noinline$privateStaticMethod() {
    System.out.println("ResolvedPackagePrivateClass.$noinline$privateStaticMethod()");
  }

  public static void $noinline$staticMethod() {
    System.out.println("ResolvedPackagePrivateClass.$noinline$staticMethod()");
  }
}
