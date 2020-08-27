/*
 * Copyright (C) 2016 The Android Open Source Project
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

public class Main {

  public static void main(String[] args) {
    instanceFieldTest();
    staticFieldTest();
    instanceFieldTest2();
  }

  /// CHECK-START: void Main.instanceFieldTest() load_store_elimination (before)
  /// CHECK:        InstanceFieldSet
  /// CHECK:        UnresolvedInstanceFieldGet

  // Load store elimination used to remove the InstanceFieldSet, thinking
  // that the UnresolvedInstanceFieldGet was not related. However inlining
  // can put you in a situation where the UnresolvedInstanceFieldGet resolves
  // to the same field as the one in InstanceFieldSet. So the InstanceFieldSet
  // must be preserved.

  /// CHECK-START: void Main.instanceFieldTest() load_store_elimination (after)
  /// CHECK:        InstanceFieldSet
  /// CHECK:        UnresolvedInstanceFieldGet
  public static void instanceFieldTest() {
    SubFoo sf = new SubFoo();
    Foo f = sf;
    f.iField = 42;
    if (sf.iField != 42) {
      throw new Error("Expected 42, got " + f.iField);
    }
  }

  /// CHECK-START: void Main.instanceFieldTest2() load_store_elimination (before)
  /// CHECK:        InstanceFieldSet
  /// CHECK:        InstanceFieldGet
  /// CHECK:        UnresolvedInstanceFieldSet
  /// CHECK:        InstanceFieldGet

  // Load store elimination will eliminate the first InstanceFieldGet because
  // it simply follows an InstanceFieldSet. It must however not eliminate the second
  // InstanceFieldGet, as the UnresolvedInstanceFieldSet might resolve to the same
  // field.

  /// CHECK-START: void Main.instanceFieldTest2() load_store_elimination (after)
  /// CHECK:        InstanceFieldSet
  /// CHECK-NOT:    InstanceFieldGet
  /// CHECK:        UnresolvedInstanceFieldSet
  /// CHECK:        InstanceFieldGet
  public static void instanceFieldTest2() {
    SubFoo sf = new SubFoo();
    Foo f = sf;
    f.iField = 42;
    int a = f.iField;
    sf.iField = 43;
    a = f.iField;
    if (a != 43) {
      throw new Error("Expected 43, got " + a);
    }
  }

  /// CHECK-START: void Main.staticFieldTest() load_store_elimination (before)
  /// CHECK:        StaticFieldSet
  /// CHECK:        StaticFieldSet
  /// CHECK:        UnresolvedStaticFieldGet

  /// CHECK-START: void Main.staticFieldTest() load_store_elimination (after)
  /// CHECK:        StaticFieldSet
  /// CHECK:        UnresolvedStaticFieldGet
  public static void staticFieldTest() {
    Foo.sField = 42;
    Foo.sField = 43;
    if (SubFoo.sField != 43) {
      throw new Error("Expected 43, got " + SubFoo.sField);
    }
  }
}

class Foo {
  public int iField;
  public static int sField;
}

// We make SubFoo implement an unresolved interface, so the SubFoo
// shall be unresolved and all field accesses through SubFoo shall
// yield unresolved field access HIR.
class SubFoo extends Foo implements MissingInterface {
}
