/*
 * Copyright (C) 2015 The Android Open Source Project
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

class Circle {
  Circle(double radius) {
    this.radius = radius;
  }
  public double getRadius() {
    return radius;
  }
  public double getArea() {
    return radius * radius * Math.PI;
  }
  private double radius;
}

class TestClass {
  static {
    sTestClassObj = new TestClass(-1, -2);
  }
  TestClass() {
  }
  TestClass(int i, int j) {
    this.i = i;
    this.j = j;
  }
  int i;
  int j;
  volatile int k;
  TestClass next;
  String str;
  byte b;
  static int si;
  static TestClass sTestClassObj;
}

class SubTestClass extends TestClass {
  int k;
}

class TestClass2 {
  int i;
  int j;
  int k;
  int l;
  int m;
}

class TestClass3 {
  float floatField = 8.0f;
  boolean test1 = true;
}

// Chosen to have different values with (x + 1) * 10 and (x - 1) * 10. This
// means we can easily make sure that different code is in fact executed on
// escape and non-escape paths.
// Negative so that high-bits will be set for all the 64-bit values allowing us
// to easily check for truncation.
class TestClass4 {
  float floatField = -3.0f;
  double doubleField = -3.0d;
  short shortField = -3;
  int intField = -3;
  byte byteField = -3;
  long longField = -3l;
}

class Finalizable {
  static boolean sVisited = false;
  static final int VALUE1 = 0xbeef;
  static final int VALUE2 = 0xcafe;
  int i;

  protected void finalize() {
    if (i != VALUE1) {
      System.out.println("Where is the beef?");
    }
    sVisited = true;
  }
}

interface Filter {
  public boolean isValid(int i);
}

public class Main {
  static void $noinline$Escape4(TestClass4 o) {
    o.floatField += 1.0f;
    o.doubleField += 1.0d;
    o.byteField += 1;
    o.shortField += 1;
    o.intField += 1;
    o.longField += 1;
  }

  static Object ESCAPE = null;
  static void $noinline$Escape(TestClass o) {
    if (o == null) {
      return;
    }
    ESCAPE = o;
    o.next.i++;
  }

  /// CHECK-START: double Main.calcCircleArea(double) load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: double Main.calcCircleArea(double) load_store_elimination (after)
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet

  static double calcCircleArea(double radius) {
    return new Circle(radius).getArea();
  }

  /// CHECK-START: int Main.test1(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test1(TestClass, TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: NullCheck
  /// CHECK-NOT: InstanceFieldGet

  // Different fields shouldn't alias.
  static int test1(TestClass obj1, TestClass obj2) {
    obj1.i = 1;
    obj2.j = 2;
    return obj1.i + obj2.j;
  }

  /// CHECK-START: int Main.test2(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test2(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: NullCheck
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet

  // Redundant store of the same value.
  static int test2(TestClass obj) {
    obj.j = 1;
    obj.j = 1;
    return obj.j;
  }

  /// CHECK-START: int Main.test3(TestClass) load_store_elimination (before)
  /// CHECK: StaticFieldGet
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test3(TestClass) load_store_elimination (after)
  /// CHECK: StaticFieldGet
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.test3(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet

  // A new allocation (even non-singleton) shouldn't alias with pre-existing values.
  static int test3(TestClass obj) {
    TestClass obj1 = TestClass.sTestClassObj;
    TestClass obj2 = new TestClass();  // Cannot alias with obj or obj1 which pre-exist.
    obj.next = obj2;  // Make obj2 a non-singleton.
    // All stores below need to stay since obj/obj1/obj2 are not singletons.
    obj.i = 1;
    obj1.j = 2;
    // Following stores won't kill values of obj.i and obj1.j.
    obj2.i = 3;
    obj2.j = 4;
    return obj.i + obj1.j + obj2.i + obj2.j;
  }

  /// CHECK-START: int Main.test4(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet
  /// CHECK-DAG: Return

  /// CHECK-START: int Main.test4(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: Return

  /// CHECK-START: int Main.test4(TestClass, boolean) load_store_elimination (after)
  /// CHECK:     NullCheck
  /// CHECK:     NullCheck
  /// CHECK-NOT: NullCheck

  /// CHECK-START: int Main.test4(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet
  /// CHECK-NOT: Phi

  // Set and merge the same value in two branches.
  static int test4(TestClass obj, boolean b) {
    if (b) {
      obj.i = 1;
    } else {
      obj.i = 1;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.test5(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:  <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:  <<Int2:i\d+>>      IntConstant 2
  /// CHECK-DAG:  <<Obj:l\d+>>       ParameterValue
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Int1>>]
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Int2>>]
  /// CHECK-DAG:  <<GetField:i\d+>>  InstanceFieldGet [{{l\d+}}]
  /// CHECK-DAG:                     Return [<<GetField>>]

  /// CHECK-START: int Main.test5(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG:  <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:  <<Int2:i\d+>>      IntConstant 2
  /// CHECK-DAG:  <<Obj:l\d+>>       ParameterValue
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Int1>>]
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Int2>>]
  /// CHECK-DAG:  <<Phi:i\d+>>       Phi [<<Arg1:i\d+>>,<<Arg2:i\d+>>]
  /// CHECK-DAG:                     Return [<<Phi>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>"]) == set(["<<Int1>>","<<Int2>>"])

  /// CHECK-START: int Main.test5(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet

  // Set and merge different values in two branches.
  static int test5(TestClass obj, boolean b) {
    if (b) {
      obj.i = 1;
    } else {
      obj.i = 2;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.test6(TestClass, TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.test6(TestClass, TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.test6(TestClass, TestClass, boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldGet
  /// CHECK-NOT: InstanceFieldGet

  // Setting the same value doesn't clear the value for aliased locations.
  static int test6(TestClass obj1, TestClass obj2, boolean b) {
    obj1.i = 1;
    obj1.j = 2;
    if (b) {
      obj2.j = 2;
    }
    return obj1.j + obj2.j;
  }

  /// CHECK-START: int Main.test7(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test7(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  // Invocation should kill values in non-singleton heap locations.
  static int test7(TestClass obj) {
    obj.i = 1;
    System.out.print("");
    return obj.i;
  }

  /// CHECK-START: int Main.test8() load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InvokeVirtual
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test8() load_store_elimination (after)
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK: InvokeVirtual
  /// CHECK-NOT: NullCheck
  /// CHECK-NOT: InstanceFieldGet

  // Invocation should not kill values in singleton heap locations.
  static int test8() {
    TestClass obj = new TestClass();
    obj.i = 1;
    System.out.print("");
    return obj.i;
  }

  /// CHECK-START: int Main.test9(TestClass) load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test9(TestClass) load_store_elimination (after)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  // Invocation should kill values in non-singleton heap locations.
  static int test9(TestClass obj) {
    TestClass obj2 = new TestClass();
    obj2.i = 1;
    obj.next = obj2;
    System.out.print("");
    return obj2.i;
  }

  /// CHECK-START: int Main.test10(TestClass) load_store_elimination (before)
  /// CHECK-DAG: StaticFieldGet
  /// CHECK-DAG: InstanceFieldGet
  /// CHECK-DAG: StaticFieldSet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.test10(TestClass) load_store_elimination (after)
  /// CHECK-DAG: StaticFieldGet
  /// CHECK-DAG: InstanceFieldGet
  /// CHECK-DAG: StaticFieldSet

  /// CHECK-START: int Main.test10(TestClass) load_store_elimination (after)
  /// CHECK:     NullCheck
  /// CHECK-NOT: NullCheck

  /// CHECK-START: int Main.test10(TestClass) load_store_elimination (after)
  /// CHECK:     InstanceFieldGet
  /// CHECK-NOT: InstanceFieldGet

  // Static fields shouldn't alias with instance fields.
  static int test10(TestClass obj) {
    TestClass.si += obj.i;
    return obj.i;
  }

  /// CHECK-START: int Main.test11(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test11(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: NullCheck
  /// CHECK-NOT: InstanceFieldGet

  // Loop without heap writes.
  static int test11(TestClass obj) {
    obj.i = 1;
    int sum = 0;
    for (int i = 0; i < 10; i++) {
      sum += obj.i;
    }
    return sum;
  }

  /// CHECK-START: int Main.test12(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.test12(TestClass, TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet

  // Loop with heap writes.
  static int test12(TestClass obj1, TestClass obj2) {
    obj1.i = 1;
    int sum = 0;
    for (int i = 0; i < 10; i++) {
      sum += obj1.i;
      obj2.i = sum;
    }
    return sum;
  }

  /// CHECK-START: int Main.test13(TestClass, TestClass2) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test13(TestClass, TestClass2) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: NullCheck
  /// CHECK-NOT: InstanceFieldGet

  // Different classes shouldn't alias.
  static int test13(TestClass obj1, TestClass2 obj2) {
    obj1.i = 1;
    obj2.i = 2;
    return obj1.i + obj2.i;
  }

  /// CHECK-START: int Main.test14(TestClass, SubTestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test14(TestClass, SubTestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  // Subclass may alias with super class.
  static int test14(TestClass obj1, SubTestClass obj2) {
    obj1.i = 1;
    obj2.i = 2;
    return obj1.i;
  }

  /// CHECK-START: int Main.test15() load_store_elimination (before)
  /// CHECK: StaticFieldSet
  /// CHECK: StaticFieldSet
  /// CHECK: StaticFieldGet

  /// CHECK-START: int Main.test15() load_store_elimination (after)
  /// CHECK: <<Const2:i\d+>> IntConstant 2
  /// CHECK: StaticFieldSet
  /// CHECK: Return [<<Const2>>]

  /// CHECK-START: int Main.test15() load_store_elimination (after)
  /// CHECK-NOT: StaticFieldGet

  // Static field access from subclass's name.
  static int test15() {
    TestClass.si = 1;
    SubTestClass.si = 2;
    return TestClass.si;
  }

  /// CHECK-START: int Main.test16() load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test16() load_store_elimination (after)
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet

  // Test inlined constructor.
  static int test16() {
    TestClass obj = new TestClass(1, 2);
    return obj.i + obj.j;
  }

  /// CHECK-START: int Main.test17() load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test17() load_store_elimination (after)
  /// CHECK: <<Const0:i\d+>> IntConstant 0
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet
  /// CHECK: Return [<<Const0>>]

  // Test getting default value.
  static int test17() {
    TestClass obj = new TestClass();
    obj.j = 1;
    return obj.i;
  }

  /// CHECK-START: int Main.test18(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test18(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  // Volatile field load/store shouldn't be eliminated.
  static int test18(TestClass obj) {
    obj.k = 1;
    return obj.k;
  }

  /// CHECK-START: float Main.test19(float[], float[]) load_store_elimination (before)
  /// CHECK:     {{f\d+}} ArrayGet
  /// CHECK:     {{f\d+}} ArrayGet

  /// CHECK-START: float Main.test19(float[], float[]) load_store_elimination (after)
  /// CHECK:     {{f\d+}} ArrayGet
  /// CHECK-NOT: {{f\d+}} ArrayGet

  // I/F, J/D aliasing should not happen any more and LSE should eliminate the load.
  static float test19(float[] fa1, float[] fa2) {
    fa1[0] = fa2[0];
    return fa1[0];
  }

  /// CHECK-START: TestClass Main.test20() load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet

  /// CHECK-START: TestClass Main.test20() load_store_elimination (after)
  /// CHECK: NewInstance
  /// CHECK-NOT: InstanceFieldSet

  // Storing default heap value is redundant if the heap location has the
  // default heap value.
  static TestClass test20() {
    TestClass obj = new TestClass();
    obj.i = 0;
    return obj;
  }

  /// CHECK-START: void Main.test21(TestClass) load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: void Main.test21(TestClass) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: Phi

  /// CHECK-START: void Main.test21(TestClass) load_store_elimination (after)
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldGet

  // Loop side effects can kill heap values, stores need to be kept in that case.
  static void test21(TestClass obj0) {
    TestClass obj = new TestClass();
    obj0.str = "abc";
    obj.str = "abc";
    // Note: This loop is transformed by the loop optimization pass, therefore we
    // are not checking the exact number of InstanceFieldSet and Phi instructions.
    for (int i = 0; i < 2; i++) {
      // Generate some loop side effect that writes into obj.
      obj.str = "def";
    }
    $noinline$printSubstrings00(obj0.str, obj.str);
  }

  static void $noinline$printSubstrings00(String str1, String str2) {
    System.out.print(str1.substring(0, 0) + str2.substring(0, 0));
  }

  /// CHECK-START: int Main.test22() load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.test22() load_store_elimination (after)
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet

  // For a singleton, loop side effects can kill its field values only if:
  // (1) it dominiates the loop header, and
  // (2) its fields are stored into inside a loop.
  static int test22() {
    int sum = 0;
    TestClass obj1 = new TestClass();
    obj1.i = 2;    // This store can be eliminated since obj1 is never stored into inside a loop.
    for (int i = 0; i < 2; i++) {
      TestClass obj2 = new TestClass();
      obj2.i = 3;  // This store can be eliminated since the singleton is inside the loop.
      sum += obj2.i;
    }
    TestClass obj3 = new TestClass();
    obj3.i = 5;    // This store can be eliminated since the singleton is created after the loop.
    sum += obj1.i + obj3.i;
    return sum;
  }

  /// CHECK-START: int Main.test23(boolean) load_store_elimination (before)
  /// CHECK-DAG:  <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:  <<Int2:i\d+>>      IntConstant 2
  /// CHECK-DAG:  <<Int3:i\d+>>      IntConstant 3
  /// CHECK-DAG:  <<Obj:l\d+>>       NewInstance
  /// CHECK-DAG:                     InstanceFieldSet [<<Obj>>,<<Int3>>]
  /// CHECK-DAG:  <<Add1:i\d+>>      Add [<<Get1:i\d+>>,<<Int1>>]
  /// CHECK-DAG:  <<Get1>>           InstanceFieldGet [<<Obj>>]
  /// CHECK-DAG:                     InstanceFieldSet [<<Obj>>,<<Add1>>]
  /// CHECK-DAG:  <<Add2:i\d+>>      Add [<<Get2:i\d+>>,<<Int2>>]
  /// CHECK-DAG:  <<Get2>>           InstanceFieldGet [<<Obj>>]
  /// CHECK-DAG:                     InstanceFieldSet [<<Obj>>,<<Add2>>]
  /// CHECK-DAG:                     Return [<<Get3:i\d+>>]
  /// CHECK-DAG:  <<Get3>>           InstanceFieldGet [<<Obj>>]

  /// CHECK-START: int Main.test23(boolean) load_store_elimination (after)
  /// CHECK-DAG:  <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:  <<Int2:i\d+>>      IntConstant 2
  /// CHECK-DAG:  <<Int3:i\d+>>      IntConstant 3
  /// CHECK-DAG:  <<Add1:i\d+>>      Add [<<Int3>>,<<Int1>>]
  /// CHECK-DAG:  <<Add2:i\d+>>      Add [<<Int3>>,<<Int2>>]
  /// CHECK-DAG:  <<Phi:i\d+>>       Phi [<<Arg1:i\d+>>,<<Arg2:i\d+>>]
  /// CHECK-DAG:                     Return [<<Phi>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>"]) == set(["<<Add1>>","<<Add2>>"])

  /// CHECK-START: int Main.test23(boolean) load_store_elimination (after)
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet

  // Test heap value merging from multiple branches.
  static int test23(boolean b) {
    TestClass obj = new TestClass();
    obj.i = 3;      // This store can be eliminated since the value flows into each branch.
    if (b) {
      obj.i += 1;   // This store can be eliminated after replacing the load below with a Phi.
    } else {
      obj.i += 2;   // This store can be eliminated after replacing the load below with a Phi.
    }
    return obj.i;   // This load is eliminated by creating a Phi.
  }

  /// CHECK-START: float Main.test24() load_store_elimination (before)
  /// CHECK-DAG:     <<True:i\d+>>     IntConstant 1
  /// CHECK-DAG:     <<Float8:f\d+>>   FloatConstant 8
  /// CHECK-DAG:     <<Float42:f\d+>>  FloatConstant 42
  /// CHECK-DAG:     <<Obj:l\d+>>      NewInstance
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<True>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float8>>]
  /// CHECK-DAG:     <<GetTest:z\d+>>  InstanceFieldGet [<<Obj>>]
  /// CHECK-DAG:     <<GetField:f\d+>> InstanceFieldGet [<<Obj>>]
  /// CHECK-DAG:     <<Select:f\d+>>   Select [<<Float42>>,<<GetField>>,<<GetTest>>]
  /// CHECK-DAG:                       Return [<<Select>>]

  /// CHECK-START: float Main.test24() load_store_elimination (after)
  /// CHECK-DAG:     <<True:i\d+>>     IntConstant 1
  /// CHECK-DAG:     <<Float8:f\d+>>   FloatConstant 8
  /// CHECK-DAG:     <<Float42:f\d+>>  FloatConstant 42
  /// CHECK-DAG:     <<Select:f\d+>>   Select [<<Float42>>,<<Float8>>,<<True>>]
  /// CHECK-DAG:                       Return [<<Select>>]

  /// CHECK-START: float Main.test24() load_store_elimination (after)
  /// CHECK-NOT:                       NewInstance
  /// CHECK-NOT:                       InstanceFieldGet
  static float test24() {
    float a = 42.0f;
    TestClass3 obj = new TestClass3();
    if (obj.test1) {
      a = obj.floatField;
    }
    return a;
  }

  /// CHECK-START: int Main.test25(boolean, boolean, boolean) load_store_elimination (before)
  /// CHECK-DAG:     <<Int1:i\d+>>     IntConstant 1
  /// CHECK-DAG:     <<Int2:i\d+>>     IntConstant 2
  /// CHECK-DAG:     <<Int3:i\d+>>     IntConstant 3
  /// CHECK-DAG:     <<Int5:i\d+>>     IntConstant 5
  /// CHECK-DAG:     <<Int6:i\d+>>     IntConstant 6
  /// CHECK-DAG:     <<Obj:l\d+>>      NewInstance
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Int1>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Int2>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Int3>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Int5>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Int6>>]
  /// CHECK-DAG:     <<GetField:i\d+>> InstanceFieldGet [<<Obj>>]
  /// CHECK-DAG:                       Return [<<GetField>>]

  /// CHECK-START: int Main.test25(boolean, boolean, boolean) load_store_elimination (after)
  /// CHECK-DAG:     <<Int1:i\d+>>     IntConstant 1
  /// CHECK-DAG:     <<Int2:i\d+>>     IntConstant 2
  /// CHECK-DAG:     <<Int3:i\d+>>     IntConstant 3
  /// CHECK-DAG:     <<Int5:i\d+>>     IntConstant 5
  /// CHECK-DAG:     <<Int6:i\d+>>     IntConstant 6
  /// CHECK-DAG:     <<Phi:i\d+>>      Phi [<<Arg1:i\d+>>,<<Arg2:i\d+>>,<<Arg3:i\d+>>,<<Arg4:i\d+>>,<<Arg5:i\d+>>]
  /// CHECK-DAG:                       Return [<<Phi>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>","<<Arg3>>","<<Arg4>>","<<Arg5>>"]) == set(["<<Int1>>","<<Int2>>","<<Int3>>","<<Int5>>","<<Int6>>"])

  /// CHECK-START: int Main.test25(boolean, boolean, boolean) load_store_elimination (after)
  /// CHECK-NOT:                       NewInstance
  /// CHECK-NOT:                       InstanceFieldSet
  /// CHECK-NOT:                       InstanceFieldGet

  // Test heap value merging from nested branches.
  static int test25(boolean b, boolean c, boolean d) {
    TestClass obj = new TestClass();
    if (b) {
      if (c) {
        obj.i = 1;
      } else {
        if (d) {
          obj.i = 2;
        } else {
          obj.i = 3;
        }
      }
    } else {
      if (c) {
        obj.i = 5;
      } else {
        obj.i = 6;
      }
    }
    return obj.i;
  }

  /// CHECK-START: float Main.test26(int) load_store_elimination (before)
  /// CHECK-DAG:     <<Float0:f\d+>>   FloatConstant 0
  /// CHECK-DAG:     <<Float1:f\d+>>   FloatConstant 1
  /// CHECK-DAG:     <<Float2:f\d+>>   FloatConstant 2
  /// CHECK-DAG:     <<Float3:f\d+>>   FloatConstant 3
  /// CHECK-DAG:     <<Float8:f\d+>>   FloatConstant 8
  /// CHECK-DAG:     <<Obj:l\d+>>      NewInstance
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float8>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float0>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float1>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float2>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float3>>]
  /// CHECK-DAG:     <<GetField:f\d+>> InstanceFieldGet [<<Obj>>]
  /// CHECK-DAG:                       Return [<<GetField>>]

  /// CHECK-START: float Main.test26(int) load_store_elimination (after)
  /// CHECK-DAG:     <<Float0:f\d+>>   FloatConstant 0
  /// CHECK-DAG:     <<Float1:f\d+>>   FloatConstant 1
  /// CHECK-DAG:     <<Float2:f\d+>>   FloatConstant 2
  /// CHECK-DAG:     <<Float3:f\d+>>   FloatConstant 3
  /// CHECK-DAG:     <<Float8:f\d+>>   FloatConstant 8
  /// CHECK-DAG:     <<Phi:f\d+>>      Phi [<<Arg1:f\d+>>,<<Arg2:f\d+>>,<<Arg3:f\d+>>,<<Arg4:f\d+>>]
  /// CHECK-DAG:                       Return [<<Phi>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>","<<Arg3>>","<<Arg4>>"]) == set(["<<Float0>>","<<Float1>>","<<Float2>>","<<Float3>>"])

  /// CHECK-START: float Main.test26(int) load_store_elimination (after)
  /// CHECK-NOT:                       NewInstance
  /// CHECK-NOT:                       InstanceFieldSet
  /// CHECK-NOT:                       InstanceFieldGet

  // Test heap value merging from switch statement.
  static float test26(int b) {
    TestClass3 obj = new TestClass3();
    switch (b) {
      case 1:
        obj.floatField = 3.0f;
        break;
      case 2:
        obj.floatField = 2.0f;
        break;
      case 3:
        obj.floatField = 1.0f;
        break;
      default:
        obj.floatField = 0.0f;
        break;
    }
    return obj.floatField;
  }

  /// CHECK-START: int Main.test27(boolean, boolean) load_store_elimination (before)
  /// CHECK-DAG:   <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:   <<Obj:l\d+>>       NewInstance
  /// CHECK-DAG:                      InstanceFieldSet [<<Obj>>,<<Int1>>]
  /// CHECK-DAG:                      InstanceFieldSet [<<Obj>>,<<Int1>>]
  /// CHECK-DAG:                      InstanceFieldSet [<<Obj>>,<<Int1>>]
  /// CHECK-DAG:                      InstanceFieldSet [<<Obj>>,<<Int1>>]
  /// CHECK-DAG:   <<GetField:i\d+>>  InstanceFieldGet [<<Obj>>]
  /// CHECK-DAG:                      Return [<<GetField>>]

  /// CHECK-START: int Main.test27(boolean, boolean) load_store_elimination (after)
  /// CHECK-DAG:   <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:                      Return [<<Int1>>]

  /// CHECK-START: int Main.test27(boolean, boolean) load_store_elimination (after)
  /// CHECK-NOT:                      NewInstance
  /// CHECK-NOT:                      InstanceFieldSet
  /// CHECK-NOT:                      InstanceFieldGet
  /// CHECK-NOT:                      Phi

  // Test merging same value from nested branches.
  static int test27(boolean b, boolean c) {
    TestClass obj = new TestClass();
    if (b) {
      if (c) {
        obj.i = 1;
      } else {
        obj.i = 1;
      }
    } else {
      if (c) {
        obj.i = 1;
      } else {
        obj.i = 1;
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.test28(boolean, boolean) load_store_elimination (before)
  /// CHECK-DAG:   <<Int0:i\d+>>      IntConstant 0
  /// CHECK-DAG:   <<Int5:i\d+>>      IntConstant 5
  /// CHECK-DAG:   <<Int6:i\d+>>      IntConstant 6
  /// CHECK-DAG:   <<Array:l\d+>>     NewArray
  /// CHECK-DAG:                      ArraySet [<<Array>>,<<Int0>>,<<Int5>>]
  /// CHECK-DAG:                      ArraySet [<<Array>>,<<Int0>>,<<Int6>>]
  /// CHECK-DAG:   <<GetIndex:i\d+>>  ArrayGet [<<Array>>,<<Int0>>]
  /// CHECK-DAG:                      Return [<<GetIndex>>]

  /// CHECK-START: int Main.test28(boolean, boolean) load_store_elimination (after)
  /// CHECK-DAG:   <<Int0:i\d+>>      IntConstant 0
  /// CHECK-DAG:   <<Int5:i\d+>>      IntConstant 5
  /// CHECK-DAG:   <<Int6:i\d+>>      IntConstant 6
  /// CHECK-DAG:   <<Phi:i\d+>>       Phi [<<Arg1:i\d+>>,<<Arg2:i\d+>>,<<Arg3:i\d+>>]
  /// CHECK-DAG:                      Return [<<Phi>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>","<<Arg3>>"]) == set(["<<Int0>>","<<Int5>>","<<Int6>>"])

  /// CHECK-START: int Main.test28(boolean, boolean) load_store_elimination (after)
  /// CHECK-NOT:                       NewArray
  /// CHECK-NOT:                       ArraySet
  /// CHECK-NOT:                       ArrayGet

  // Test merging array stores in branches.
  static int test28(boolean b, boolean c) {
    int[] array = new int[1];
    if (b) {
      if (c) {
        array[0] = 5;
      } else {
        array[0] = 6;
      }
    } else { /* Default value: 0. */ }
    return array[0];
  }

  /// CHECK-START: float Main.test29(boolean) load_store_elimination (before)
  /// CHECK-DAG:     <<Float2:f\d+>>   FloatConstant 2
  /// CHECK-DAG:     <<Float5:f\d+>>   FloatConstant 5
  /// CHECK-DAG:     <<Float8:f\d+>>   FloatConstant 8
  /// CHECK-DAG:     <<Obj:l\d+>>      NewInstance
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float8>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float2>>]
  /// CHECK-DAG:                       InstanceFieldSet [<<Obj>>,<<Float5>>]
  /// CHECK-DAG:     <<GetField:f\d+>> InstanceFieldGet [<<Obj>>]
  /// CHECK-DAG:                       Return [<<GetField>>]

  /// CHECK-START: float Main.test29(boolean) load_store_elimination (after)
  /// CHECK-DAG:     <<Float2:f\d+>>   FloatConstant 2
  /// CHECK-DAG:     <<Float5:f\d+>>   FloatConstant 5
  /// CHECK-DAG:     <<Float8:f\d+>>   FloatConstant 8
  /// CHECK-DAG:     <<Phi:f\d+>>      Phi [<<Arg1:f\d+>>,<<Arg2:f\d+>>]
  /// CHECK-DAG:                       Return [<<Phi>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>"]) == set(["<<Float5>>","<<Float2>>"])

  /// CHECK-START: float Main.test29(boolean) load_store_elimination (after)
  /// CHECK-NOT:                       NewInstance
  /// CHECK-NOT:                       InstanceFieldSet
  /// CHECK-NOT:                       InstanceFieldGet

  // Test implicit type conversion in branches.
  static float test29(boolean b) {
    TestClass3 obj = new TestClass3();
    if (b) {
      obj.floatField = 5; // Int
    } else {
      obj.floatField = 2L; // Long
    }
    return obj.floatField;
  }

  /// CHECK-START: int Main.test30(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:  <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:  <<Int2:i\d+>>      IntConstant 2
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Int1>>]
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Int2>>]
  /// CHECK-DAG:  <<GetField:i\d+>>  InstanceFieldGet [{{l\d+}}]
  /// CHECK-DAG:                     Return [<<GetField>>]

  /// CHECK-START: int Main.test30(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG:  <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:  <<Int2:i\d+>>      IntConstant 2
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Int1>>]
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Int2>>]
  /// CHECK-DAG:  <<GetField:i\d+>>  InstanceFieldGet [{{l\d+}}]
  /// CHECK-DAG:                     Return [<<GetField>>]

  /// CHECK-START: int Main.test30(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: Phi

  // Don't merge different values in two branches for different variables.
  static int test30(TestClass obj, boolean b) {
    if (b) {
      obj.i = 1;
    } else {
      obj.j = 2;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.test31(boolean, boolean) load_store_elimination (before)
  /// CHECK-DAG:  <<Int2:i\d+>>  IntConstant 2
  /// CHECK-DAG:  <<Int5:i\d+>>  IntConstant 5
  /// CHECK-DAG:  <<Int6:i\d+>>  IntConstant 6
  /// CHECK-DAG:                 InstanceFieldSet [{{l\d+}},<<Int5>>] field_name:{{.*TestClass.i}}
  /// CHECK-DAG:                 InstanceFieldSet [{{l\d+}},<<Int6>>] field_name:{{.*TestClass.i}}
  /// CHECK-DAG:  <<Get1:i\d+>>  InstanceFieldGet [{{l\d+}}] field_name:{{.*TestClass.i}}
  /// CHECK-DAG:                 InstanceFieldSet [{{l\d+}},<<Get1>>] field_name:{{.*TestClass.j}}
  /// CHECK-DAG:                 InstanceFieldSet [{{l\d+}},<<Int2>>] field_name:{{.*TestClass.i}}
  /// CHECK-DAG:  <<Get2:i\d+>>  InstanceFieldGet [{{l\d+}}]
  /// CHECK-DAG:                 Return [<<Get2>>]

  /// CHECK-START: int Main.test31(boolean, boolean) load_store_elimination (after)
  /// CHECK-DAG:  <<Int2:i\d+>>  IntConstant 2
  /// CHECK-DAG:  <<Int5:i\d+>>  IntConstant 5
  /// CHECK-DAG:  <<Int6:i\d+>>  IntConstant 6
  /// CHECK-DAG:  <<Phi1:i\d+>>  Phi [<<Int5>>,<<Int6>>]
  /// CHECK-DAG:  <<Phi2:i\d+>>  Phi [<<Phi1>>,<<Int2>>]
  /// CHECK-DAG:                 Return [<<Phi2>>]

  /// CHECK-START: int Main.test31(boolean, boolean) load_store_elimination (after)
  /// CHECK-NOT:                 NewInstance
  /// CHECK-NOT:                 InstanceFieldSet
  /// CHECK-NOT:                 InstanceFieldGet

  // Test nested branches that can't be flattened.
  static int test31(boolean b, boolean c) {
    TestClass obj = new TestClass();
    if (b) {
      if (c) {
        obj.i = 5;
      } else {
        obj.i = 6;
      }
      obj.j = obj.i;
    } else {
      obj.i = 2;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.test32(int) load_store_elimination (before)
  /// CHECK-DAG:  <<Int1:i\d+>>      IntConstant 1
  /// CHECK-DAG:  <<Int10:i\d+>>     IntConstant 10
  /// CHECK-DAG:  InstanceFieldSet [{{l\d+}},<<Int1>>] field_name:{{.*TestClass2.i}}
  /// CHECK-DAG:  InstanceFieldSet [{{l\d+}},<<Int1>>] field_name:{{.*TestClass2.j}}
  /// CHECK-DAG:  InstanceFieldSet [{{l\d+}},<<Int1>>] field_name:{{.*TestClass2.k}}
  /// CHECK-DAG:  InstanceFieldSet [{{l\d+}},<<Int1>>] field_name:{{.*TestClass2.l}}
  /// CHECK-DAG:  InstanceFieldSet [{{l\d+}},<<Int1>>] field_name:{{.*TestClass2.m}}
  /// CHECK-DAG:                     Return [<<Int10>>]

  /// CHECK-START: int Main.test32(int) load_store_elimination (after)
  /// CHECK-DAG:  <<Int10:i\d+>>     IntConstant 10
  /// CHECK-DAG:                     Return [<<Int10>>]

  /// CHECK-START: int Main.test32(int) load_store_elimination (after)
  /// CHECK-NOT:                     NewInstance
  /// CHECK-NOT:                     InstanceFieldGet
  /// CHECK-NOT:                     InstanceFieldSet
  /// CHECK-NOT:                     Phi

  // Test no unused Phi instructions are created.
  static int test32(int i) {
    TestClass2 obj = new TestClass2();
    // By default, i/j/k/l/m are initialized to 0.
    switch (i) {
      case 1: obj.i = 1; break;
      case 2: obj.j = 1; break;
      case 3: obj.k = 1; break;
      case 4: obj.l = 1; break;
      case 5: obj.m = 1; break;
    }
    // So here, each variable has value Phi [0,1,1,1,1,1].
    // But since no heap values are used, we should not be creating these Phis.
    return 10;
  }

  /// CHECK-START: int Main.test33(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG: <<Phi:i\d+>>        Phi
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Phi>>]

  /// CHECK-START: int Main.test33(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.test33(TestClass, boolean) load_store_elimination (after)
  /// CHECK:                         InstanceFieldSet
  /// CHECK-NOT:                     InstanceFieldSet

  // Test eliminating non-observable stores.
  static int test33(TestClass obj, boolean x) {
    int phi;
    if (x) {
      obj.i = 1;
      phi = 1;
    } else {
      obj.i = 2;
      phi = 2;
    }
    obj.i = phi;
    return phi;
  }

  /// CHECK-START: int Main.test34(TestClass, boolean, boolean) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG: <<Phi:i\d+>>        Phi
  /// CHECK-DAG:                     InstanceFieldSet [{{l\d+}},<<Phi>>]

  /// CHECK-START: int Main.test34(TestClass, boolean, boolean) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.test34(TestClass, boolean, boolean) load_store_elimination (after)
  /// CHECK:                         InstanceFieldSet
  /// CHECK:                         InstanceFieldSet
  /// CHECK-NOT:                     InstanceFieldSet

  // Test eliminating a store that writes a Phi equivalent to merged
  // heap values of observable stores.
  static int test34(TestClass obj, boolean x, boolean y) {
    int phi;
    if (x) {
      obj.i = 1;
      phi = 1;
      if (y) {
        return 3;
      }
    } else {
      obj.i = 2;
      phi = 2;
      if (y) {
        return 4;
      }
    }
    obj.i = phi;
    return phi;
  }

  /// CHECK-START: int Main.test35(TestClass, boolean, boolean) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.test35(TestClass, boolean, boolean) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.test35(TestClass, boolean, boolean) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  // Test Phi creation for load elimination.
  static int test35(TestClass obj, boolean x, boolean y) {
    if (x) {
      obj.i = 1;
    } else {
      obj.i = 2;
    }
    if (y) {
      if (x) {
        obj.i = 3;
      }
      obj.j = 5;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.test36(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.test36(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.test36(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  /// CHECK-START: int Main.test36(TestClass, boolean) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  // Test Phi matching for load elimination.
  static int test36(TestClass obj, boolean x) {
    int phi;
    if (x) {
      obj.i = 1;
      phi = 1;
    } else {
      obj.i = 2;
      phi = 2;
    }
    // The load is replaced by the existing Phi instead of constructing a new one.
    return obj.i + phi;
  }

  /// CHECK-START: int Main.test37(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.test37(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet

  // Test preserving observable stores.
  static int test37(TestClass obj, boolean x) {
    if (x) {
      obj.i = 1;
    }
    int tmp = obj.i;  // The store above must be kept.
    obj.i = 2;
    return tmp;
  }

  /// CHECK-START: int Main.test38(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.test38(TestClass, boolean) load_store_elimination (after)
  /// CHECK:                         InstanceFieldSet
  /// CHECK-NOT:                     InstanceFieldSet

  // Test eliminating store of the same value after eliminating non-observable stores.
  static int test38(TestClass obj, boolean x) {
    obj.i = 1;
    if (x) {
      return 1;  // The store above must be kept.
    }
    obj.i = 2;  // Not observable, shall be eliminated.
    obj.i = 3;  // Not observable, shall be eliminated.
    obj.i = 1;  // After eliminating the non-observable stores above, this stores the
                // same value that is already stored in `obj.i` and shall be eliminated.
    return 2;
  }

  /// CHECK-START: int Main.test39(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.test39(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.test39(TestClass, boolean) load_store_elimination (after)
  /// CHECK:                         InstanceFieldGet
  /// CHECK-NOT:                     InstanceFieldGet

  // Test creating a reference Phi for load elimination.
  static int test39(TestClass obj, boolean x) {
    obj.next = new TestClass(1, 2);
    if (x) {
      obj.next = new SubTestClass();
    }
    return obj.next.i;
  }

  /// CHECK-START: int Main.$noinline$testConversion1(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testConversion1(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     TypeConversion
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.$noinline$testConversion1(TestClass, int) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  // Test tracking values containing type conversion.
  // Regression test for b/161521389 .
  static int $noinline$testConversion1(TestClass obj, int x) {
    obj.i = x;
    if ((x & 1) != 0) {
      obj.b = (byte) x;
      obj.i = obj.b;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.$noinline$testConversion2(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     TypeConversion
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testConversion2(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     TypeConversion
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.$noinline$testConversion2(TestClass, int) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  /// CHECK-START: int Main.$noinline$testConversion2(TestClass, int) load_store_elimination (after)
  /// CHECK:                         TypeConversion
  /// CHECK-NOT:                     TypeConversion

  /// CHECK-START: int Main.$noinline$testConversion2(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  // Test moving type conversion when needed.
  static int $noinline$testConversion2(TestClass obj, int x) {
    int tmp = 0;
    obj.i = x;
    if ((x & 1) != 0) {
      // The instruction simplifier can remove this TypeConversion if there are
      // no environment uses. Currently, there is an environment use in NullCheck,
      // so this TypeConversion remains and GVN removes the second TypeConversion
      // below. Since we really want to test that the TypeConversion from below
      // can be moved and used for the load of `obj.b`, we have a similar test
      // written in smali in 530-checker-lse3, StoreLoad.test3(int), except that
      // it's using static fields (which would not help with the environment use).
      obj.b = (byte) x;
      obj.i = obj.b;
      tmp = (byte) x;
    }
    return obj.i + tmp;
  }

  /// CHECK-START: int Main.$noinline$testConversion3(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testConversion3(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     TypeConversion
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testConversion3(TestClass, int) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  /// CHECK-START: int Main.$noinline$testConversion3(TestClass, int) load_store_elimination (after)
  /// CHECK:                         TypeConversion
  /// CHECK-NOT:                     TypeConversion

  /// CHECK-START: int Main.$noinline$testConversion3(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  // Test tracking values containing type conversion with loop.
  static int $noinline$testConversion3(TestClass obj, int x) {
    obj.i = x;
    for (int i = 0; i < x; ++i) {
      obj.b = (byte) i;
      obj.i = obj.b;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.$noinline$testConversion4(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     TypeConversion
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testConversion4(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     TypeConversion
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testConversion4(TestClass, int) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  /// CHECK-START: int Main.$noinline$testConversion4(TestClass, int) load_store_elimination (after)
  /// CHECK:                         TypeConversion
  /// CHECK-NOT:                     TypeConversion

  /// CHECK-START: int Main.$noinline$testConversion4(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  // Test moving type conversion when needed with loop.
  static int $noinline$testConversion4(TestClass obj, int x) {
    int tmp = x;
    obj.i = x;
    for (int i = 0; i < x; ++i) {
      obj.b = (byte) i;
      obj.i = obj.b;
      tmp = (byte) i;
    }
    return obj.i + tmp;
  }

  /// CHECK-START: void Main.testFinalizable() load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: void Main.testFinalizable() load_store_elimination (after)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet

  // Allocations of finalizable objects cannot be eliminated.
  static void testFinalizable() {
    Finalizable finalizable = new Finalizable();
    finalizable.i = Finalizable.VALUE2;
    finalizable.i = Finalizable.VALUE1;
  }

  static java.lang.ref.WeakReference<Object> getWeakReference() {
    return new java.lang.ref.WeakReference<>(new Object());
  }

  static void testFinalizableByForcingGc() {
    testFinalizable();
    java.lang.ref.WeakReference<Object> reference = getWeakReference();

    Runtime runtime = Runtime.getRuntime();
    for (int i = 0; i < 20; ++i) {
      runtime.gc();
      System.runFinalization();
      try {
        Thread.sleep(1);
      } catch (InterruptedException e) {
        throw new AssertionError(e);
      }

      // Check to see if the weak reference has been garbage collected.
      if (reference.get() == null) {
        // A little bit more sleep time to make sure.
        try {
          Thread.sleep(100);
        } catch (InterruptedException e) {
          throw new AssertionError(e);
        }
        if (!Finalizable.sVisited) {
          System.out.println("finalize() not called.");
        }
        return;
      }
    }
    System.out.println("testFinalizableByForcingGc() failed to force gc.");
  }

  /// CHECK-START: int Main.$noinline$testHSelect(boolean) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: Select

  /// CHECK-START: int Main.$noinline$testHSelect(boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: Select

  // Test that HSelect creates alias.
  static int $noinline$testHSelect(boolean b) {
    TestClass obj = new TestClass();
    TestClass obj2 = null;
    obj.i = 0xdead;
    if (b) {
      obj2 = obj;
    }
    return obj2.i;
  }

  static int sumWithFilter(int[] array, Filter f) {
    int sum = 0;
    for (int i = 0; i < array.length; i++) {
      if (f.isValid(array[i])) {
        sum += array[i];
      }
    }
    return sum;
  }

  /// CHECK-START: int Main.sumWithinRange(int[], int, int) load_store_elimination (before)
  /// CHECK-DAG: NewInstance
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.sumWithinRange(int[], int, int) load_store_elimination (after)
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet

  // A lambda-style allocation can be eliminated after inlining.
  static int sumWithinRange(int[] array, final int low, final int high) {
    Filter filter = new Filter() {
      public boolean isValid(int i) {
        return (i >= low) && (i <= high);
      }
    };
    return sumWithFilter(array, filter);
  }

  private static int mI = 0;
  private static float mF = 0f;

  /// CHECK-START: float Main.testAllocationEliminationWithLoops() load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: NewInstance
  /// CHECK: NewInstance

  /// CHECK-START: float Main.testAllocationEliminationWithLoops() load_store_elimination (after)
  /// CHECK-NOT: NewInstance

  private static float testAllocationEliminationWithLoops() {
    for (int i0 = 0; i0 < 5; i0++) {
      for (int i1 = 0; i1 < 5; i1++) {
        for (int i2 = 0; i2 < 5; i2++) {
          int lI0 = ((int) new Integer(((int) new Integer(mI))));
          if (((boolean) new Boolean(false))) {
            for (int i3 = 576 - 1; i3 >= 0; i3--) {
              mF -= 976981405.0f;
            }
          }
        }
      }
    }
    return 1.0f;
  }

  /// CHECK-START: TestClass2 Main.testStoreStore() load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: TestClass2 Main.testStoreStore() load_store_elimination (after)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet

  private static TestClass2 testStoreStore() {
    TestClass2 obj = new TestClass2();
    obj.i = 41;
    obj.j = 42;
    obj.i = 41;
    obj.j = 43;
    return obj;
  }

  /// CHECK-START: void Main.testStoreStore2(TestClass2) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: void Main.testStoreStore2(TestClass2) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet

  private static void testStoreStore2(TestClass2 obj) {
    obj.i = 41;
    obj.j = 42;
    obj.i = 43;
    obj.j = 44;
  }

  /// CHECK-START: void Main.testStoreStore3(TestClass2, boolean) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: void Main.testStoreStore3(TestClass2, boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet

  /// CHECK-START: void Main.testStoreStore3(TestClass2, boolean) load_store_elimination (after)
  /// CHECK-NOT: Phi

  private static void testStoreStore3(TestClass2 obj, boolean flag) {
    obj.i = 41;
    obj.j = 42;    // redundant since it's overwritten in both branches below.
    if (flag) {
      obj.j = 43;
    } else {
      obj.j = 44;
    }
  }

  /// CHECK-START: void Main.testStoreStore4() load_store_elimination (before)
  /// CHECK: StaticFieldSet
  /// CHECK: StaticFieldSet

  /// CHECK-START: void Main.testStoreStore4() load_store_elimination (after)
  /// CHECK: StaticFieldSet
  /// CHECK-NOT: StaticFieldSet

  private static void testStoreStore4() {
    TestClass.si = 61;
    TestClass.si = 62;
  }

  /// CHECK-START: int Main.testStoreStore5(TestClass2, TestClass2) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.testStoreStore5(TestClass2, TestClass2) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet

  private static int testStoreStore5(TestClass2 obj1, TestClass2 obj2) {
    obj1.i = 71;      // This store is needed since obj2.i may load from it.
    int i = obj2.i;
    obj1.i = 72;
    return i;
  }

  /// CHECK-START: int Main.testStoreStore6(TestClass2, TestClass2) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.testStoreStore6(TestClass2, TestClass2) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet

  private static int testStoreStore6(TestClass2 obj1, TestClass2 obj2) {
    obj1.i = 81;      // This store is not needed since obj2.j cannot load from it.
    int j = obj2.j;
    obj1.i = 82;
    return j;
  }

  /// CHECK-START: int Main.testNoSideEffects(int[]) load_store_elimination (before)
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArrayGet

  /// CHECK-START: int Main.testNoSideEffects(int[]) load_store_elimination (after)
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK-NOT: ArraySet
  /// CHECK-NOT: ArrayGet

  private static int testNoSideEffects(int[] array) {
    array[0] = 101;
    array[1] = 102;
    int bitCount = Integer.bitCount(0x3456);
    array[1] = 103;
    return array[0] + bitCount;
  }

  /// CHECK-START: void Main.testThrow(TestClass2, java.lang.Exception) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: Throw

  /// CHECK-START: void Main.testThrow(TestClass2, java.lang.Exception) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: Throw

  // Make sure throw keeps the store.
  private static void testThrow(TestClass2 obj, Exception e) throws Exception {
    obj.i = 55;
    throw e;
  }

  /// CHECK-START: int Main.testStoreStoreWithDeoptimize(int[]) load_store_elimination (before)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: Deoptimize
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArrayGet
  /// CHECK: ArrayGet
  /// CHECK: ArrayGet
  /// CHECK: ArrayGet

  /// CHECK-START: int Main.testStoreStoreWithDeoptimize(int[]) load_store_elimination (after)
  /// CHECK: NewInstance
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK: Deoptimize
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK-NOT: ArrayGet

  private static int testStoreStoreWithDeoptimize(int[] arr) {
    TestClass2 obj = new TestClass2();
    obj.i = 41;
    obj.j = 42;
    obj.i = 41;
    obj.j = 43;
    arr[0] = 1;  // One HDeoptimize here.
    arr[1] = 1;
    arr[2] = 1;
    arr[3] = 1;
    return arr[0] + arr[1] + arr[2] + arr[3];
  }

  /// CHECK-START: double Main.getCircleArea(double, boolean) load_store_elimination (before)
  /// CHECK: NewInstance

  /// CHECK-START: double Main.getCircleArea(double, boolean) load_store_elimination (after)
  /// CHECK-NOT: NewInstance

  private static double getCircleArea(double radius, boolean b) {
    double area = 0d;
    if (b) {
      area = new Circle(radius).getArea();
    }
    return area;
  }

  /// CHECK-START: double Main.testDeoptimize(int[], double[], double) load_store_elimination (before)
  /// CHECK: Deoptimize
  /// CHECK: NewInstance
  /// CHECK: Deoptimize
  /// CHECK: NewInstance

  /// CHECK-START: double Main.testDeoptimize(int[], double[], double) load_store_elimination (after)
  /// CHECK: Deoptimize
  /// CHECK: NewInstance
  /// CHECK: Deoptimize
  /// CHECK-NOT: NewInstance

  private static double testDeoptimize(int[] iarr, double[] darr, double radius) {
    iarr[0] = 1;  // One HDeoptimize here. Not triggered.
    iarr[1] = 1;
    Circle circle1 = new Circle(radius);
    iarr[2] = 1;
    darr[0] = circle1.getRadius();  // One HDeoptimize here, which holds circle1 live. Triggered.
    darr[1] = circle1.getRadius();
    darr[2] = circle1.getRadius();
    darr[3] = circle1.getRadius();
    return new Circle(Math.PI).getArea();
  }

  /// CHECK-START: int Main.testAllocationEliminationOfArray1() load_store_elimination (before)
  /// CHECK: NewArray
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArrayGet
  /// CHECK: ArrayGet
  /// CHECK: ArrayGet
  /// CHECK: ArrayGet

  /// CHECK-START: int Main.testAllocationEliminationOfArray1() load_store_elimination (after)
  /// CHECK-NOT: NewArray
  /// CHECK-NOT: ArraySet
  /// CHECK-NOT: ArrayGet
  private static int testAllocationEliminationOfArray1() {
    int[] array = new int[4];
    array[2] = 4;
    array[3] = 7;
    return array[0] + array[1] + array[2] + array[3];
  }

  /// CHECK-START: int Main.testAllocationEliminationOfArray2() load_store_elimination (before)
  /// CHECK: NewArray
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArrayGet

  /// CHECK-START: int Main.testAllocationEliminationOfArray2() load_store_elimination (after)
  /// CHECK: NewArray
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArrayGet
  private static int testAllocationEliminationOfArray2() {
    // Cannot eliminate array allocation since array is accessed with non-constant
    // index (only 3 elements to prevent vectorization of the reduction).
    int[] array = new int[3];
    array[1] = 4;
    array[2] = 7;
    int sum = 0;
    for (int e : array) {
      sum += e;
    }
    return sum;
  }

  /// CHECK-START: int Main.testAllocationEliminationOfArray3(int) load_store_elimination (before)
  /// CHECK: NewArray
  /// CHECK: ArraySet
  /// CHECK: ArrayGet

  /// CHECK-START: int Main.testAllocationEliminationOfArray3(int) load_store_elimination (after)
  /// CHECK-NOT: NewArray
  /// CHECK-NOT: ArraySet
  /// CHECK-NOT: ArrayGet
  private static int testAllocationEliminationOfArray3(int i) {
    int[] array = new int[4];
    array[i] = 4;
    return array[i];
  }

  /// CHECK-START: int Main.testAllocationEliminationOfArray4(int) load_store_elimination (before)
  /// CHECK: NewArray
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArrayGet
  /// CHECK: ArrayGet

  /// CHECK-START: int Main.testAllocationEliminationOfArray4(int) load_store_elimination (after)
  /// CHECK: NewArray
  /// CHECK: ArraySet
  /// CHECK: ArraySet
  /// CHECK: ArrayGet
  /// CHECK-NOT: ArrayGet
  private static int testAllocationEliminationOfArray4(int i) {
    // Cannot eliminate array allocation due to index aliasing between 1 and i.
    int[] array = new int[4];
    array[1] = 2;
    array[i] = 4;
    return array[1] + array[i];
  }

  /// CHECK-START: int Main.testAllocationEliminationOfArray5(int) load_store_elimination (before)
  /// CHECK: NewArray
  /// CHECK: ArraySet
  /// CHECK: ArrayGet

  /// CHECK-START: int Main.testAllocationEliminationOfArray5(int) load_store_elimination (after)
  /// CHECK: NewArray
  /// CHECK-NOT: ArraySet
  /// CHECK-NOT: ArrayGet
  private static int testAllocationEliminationOfArray5(int i) {
    // Cannot eliminate array allocation due to unknown i that may
    // cause NegativeArraySizeException.
    int[] array = new int[i];
    array[1] = 12;
    return array[1];
  }

  /// CHECK-START: int Main.testExitMerge(boolean) load_store_elimination (before)
  /// CHECK-DAG: NewInstance
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet
  /// CHECK-DAG: Return
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: Throw

  /// CHECK-START: int Main.testExitMerge(boolean) load_store_elimination (after)
  /// CHECK-DAG: Return
  /// CHECK-DAG: Throw

  /// CHECK-START: int Main.testExitMerge(boolean) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet

  /// CHECK-START: int Main.testExitMerge(boolean) load_store_elimination (after)
  /// CHECK: NewInstance
  /// CHECK-NOT: NewInstance
  private static int testExitMerge(boolean cond) {
    TestClass obj = new TestClass();
    if (cond) {
      obj.i = 1;
      return obj.i + 1;
    } else {
      obj.i = 2;
      throw new Error();  // Note: We have a NewInstance here.
    }
  }

  /// CHECK-START: int Main.testExitMerge2(boolean) load_store_elimination (before)
  /// CHECK-DAG: NewInstance
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.testExitMerge2(boolean) load_store_elimination (after)
  /// CHECK-NOT: NewInstance
  /// CHECK-NOT: InstanceFieldSet
  /// CHECK-NOT: InstanceFieldGet
  private static int testExitMerge2(boolean cond) {
    TestClass obj = new TestClass();
    int res;
    if (cond) {
      obj.i = 1;
      res = obj.i + 1;
    } else {
      obj.i = 2;
      res = obj.j + 2;
    }
    return res;
  }

  /// CHECK-START: void Main.testStoreSameValue() load_store_elimination (before)
  /// CHECK: NewArray
  /// CHECK: ArrayGet
  /// CHECK: ArraySet

  /// CHECK-START: void Main.testStoreSameValue() load_store_elimination (after)
  /// CHECK: NewArray
  /// CHECK-NOT: ArrayGet
  /// CHECK-NOT: ArraySet
  private static void testStoreSameValue() {
    Object[] array = new Object[2];
    sArray = array;
    Object obj = array[0];
    array[1] = obj;    // Store the same value as the default value.
  }

  /// CHECK-START: int Main.$noinline$testByteArrayDefaultValue() load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG: <<Value:b\d+>>  ArrayGet
  /// CHECK-DAG:                 Return [<<Value>>]

  /// CHECK-START: int Main.$noinline$testByteArrayDefaultValue() load_store_elimination (after)
  /// CHECK-DAG: <<Const0:i\d+>> IntConstant 0
  /// CHECK-DAG:                 Return [<<Const0>>]

  /// CHECK-START: int Main.$noinline$testByteArrayDefaultValue() load_store_elimination (after)
  /// CHECK-NOT:                 NewArray
  /// CHECK-NOT:                 ArrayGet
  /// CHECK-NOT:                 TypeConversion
  private static int $noinline$testByteArrayDefaultValue() {
    byte[] array = new byte[2];
    array[1] = 1;  // FIXME: Without any stores, LSA tells LSE not to run.
    return array[0];
  }

  static Object[] sArray;

  /// CHECK-START: int Main.testLocalArrayMerge1(boolean) load_store_elimination (before)
  /// CHECK-DAG: <<Const0:i\d+>> IntConstant 0
  /// CHECK-DAG: <<Const1:i\d+>> IntConstant 1
  /// CHECK-DAG: <<A:l\d+>>      NewArray
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const0>>]
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const1>>]
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const1>>]
  /// CHECK-DAG: <<Get:i\d+>>    ArrayGet [<<A>>,<<Const0>>]
  /// CHECK-DAG:                 Return [<<Get>>]
  //
  /// CHECK-START: int Main.testLocalArrayMerge1(boolean) load_store_elimination (after)
  /// CHECK-DAG: <<Const1:i\d+>> IntConstant 1
  /// CHECK-DAG:                 Return [<<Const1>>]
  //
  /// CHECK-START: int Main.testLocalArrayMerge1(boolean) load_store_elimination (after)
  /// CHECK-NOT:                 NewArray
  /// CHECK-NOT:                 ArraySet
  /// CHECK-NOT:                 ArrayGet
  private static int testLocalArrayMerge1(boolean x) {
    // The explicit store can be removed right away
    // since it is equivalent to the default.
    int[] a = { 0 };
    // The diamond pattern stores/load can be replaced
    // by the direct value.
    if (x) {
      a[0] = 1;
    } else {
      a[0] = 1;
    }
    return a[0];
  }

  /// CHECK-START: int Main.testLocalArrayMerge2(boolean) load_store_elimination (before)
  /// CHECK-DAG: <<Const0:i\d+>> IntConstant 0
  /// CHECK-DAG: <<Const1:i\d+>> IntConstant 1
  /// CHECK-DAG: <<Const2:i\d+>> IntConstant 2
  /// CHECK-DAG: <<Const3:i\d+>> IntConstant 3
  /// CHECK-DAG: <<A:l\d+>>      NewArray
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const1>>]
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const2>>]
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const3>>]
  /// CHECK-DAG: <<Get:i\d+>>    ArrayGet [<<A>>,<<Const0>>]
  /// CHECK-DAG:                 Return [<<Get>>]

  /// CHECK-START: int Main.testLocalArrayMerge2(boolean) load_store_elimination (after)
  /// CHECK-DAG: <<Const2:i\d+>> IntConstant 2
  /// CHECK-DAG: <<Const3:i\d+>> IntConstant 3
  /// CHECK-DAG: <<Phi:i\d+>>    Phi [<<Arg1:i\d+>>,<<Arg2:i\d+>>]
  /// CHECK-DAG:                 Return [<<Phi>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>"]) == set(["<<Const2>>","<<Const3>>"])

  /// CHECK-START: int Main.testLocalArrayMerge2(boolean) load_store_elimination (after)
  /// CHECK-NOT:                 NewArray
  /// CHECK-NOT:                 ArraySet
  /// CHECK-NOT:                 ArrayGet
  private static int testLocalArrayMerge2(boolean x) {
    // The explicit store can be removed eventually even
    // though it is not equivalent to the default.
    int[] a = { 1 };
    // The load after the diamond pattern is eliminated and replaced with a Phi,
    // stores are then also eliminated.
    if (x) {
      a[0] = 2;
    } else {
      a[0] = 3;
    }
    return a[0];
  }

  /// CHECK-START: int Main.testLocalArrayMerge3(boolean) load_store_elimination (before)
  /// CHECK-DAG: <<Const0:i\d+>> IntConstant 0
  /// CHECK-DAG: <<Const1:i\d+>> IntConstant 1
  /// CHECK-DAG: <<Const2:i\d+>> IntConstant 2
  /// CHECK-DAG: <<A:l\d+>>      NewArray
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const1>>]
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const2>>]
  /// CHECK-DAG: <<Get:i\d+>>    ArrayGet [<<A>>,<<Const0>>]
  /// CHECK-DAG:                 Return [<<Get>>]

  /// CHECK-START: int Main.testLocalArrayMerge3(boolean) load_store_elimination (after)
  /// CHECK-NOT:                 NewArray
  /// CHECK-NOT:                 ArraySet
  /// CHECK-NOT:                 ArrayGet
  private static int testLocalArrayMerge3(boolean x) {
    int[] a = { 1 };
    if (x) {
      a[0] = 2;
    }
    return a[0];
  }

  /// CHECK-START: int Main.testLocalArrayMerge4(boolean) load_store_elimination (before)
  /// CHECK-DAG: <<Const0:i\d+>> IntConstant 0
  /// CHECK-DAG: <<Const1:i\d+>> IntConstant 1
  /// CHECK-DAG: <<A:l\d+>>      NewArray
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const0>>]
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const1>>]
  /// CHECK-DAG:                 ArraySet [<<A>>,<<Const0>>,<<Const1>>]
  /// CHECK-DAG: <<Get1:b\d+>>   ArrayGet [<<A>>,<<Const0>>]
  /// CHECK-DAG: <<Get2:a\d+>>   ArrayGet [<<A>>,<<Const0>>]
  /// CHECK-DAG: <<Add:i\d+>>    Add [<<Get1>>,<<Get2>>]
  /// CHECK-DAG:                 Return [<<Add>>]
  //
  /// CHECK-START: int Main.testLocalArrayMerge4(boolean) load_store_elimination (after)
  /// CHECK-DAG: <<Const1:i\d+>> IntConstant 1
  /// CHECK-DAG: <<Cnv1:b\d+>>   TypeConversion [<<Const1>>]
  /// CHECK-DAG: <<Cnv2:a\d+>>   TypeConversion [<<Const1>>]
  /// CHECK-DAG: <<Add:i\d+>>    Add [<<Cnv1>>,<<Cnv2>>]
  /// CHECK-DAG:                 Return [<<Add>>]
  //
  /// CHECK-START: int Main.testLocalArrayMerge4(boolean) load_store_elimination (after)
  /// CHECK-NOT:                 NewArray
  /// CHECK-NOT:                 ArraySet
  /// CHECK-NOT:                 ArrayGet
  private static int testLocalArrayMerge4(boolean x) {
    byte[] a = { 0 };
    if (x) {
      a[0] = 1;
    } else {
      a[0] = 1;
    }
    // Differently typed (signed vs unsigned),
    // but same reference.
    return a[0] + (a[0] & 0xff);
  }

  /// CHECK-START: int Main.testLocalArrayMerge5(int[], boolean) load_store_elimination (before)
  /// CHECK:                     ArraySet
  /// CHECK:                     ArraySet
  /// CHECK:                     ArraySet

  /// CHECK-START: int Main.testLocalArrayMerge5(int[], boolean) load_store_elimination (after)
  /// CHECK-NOT:                 ArraySet

  // Test eliminating store of the same value after eliminating non-observable stores.
  private static int testLocalArrayMerge5(int[] a, boolean x) {
    int old = a[0];
    if (x) {
      a[0] = 1;
    } else {
      a[0] = 1;
    }
    // This store makes the stores above dead and they will be eliminated.
    // That makes this store unnecessary as we're storing the same value already
    // present in this location, so it shall also be eliminated.
    a[0] = old;
    return old;
  }

  /// CHECK-START: int Main.testLocalArrayMerge6(int[], boolean, boolean) load_store_elimination (before)
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLocalArrayMerge6(int[], boolean, boolean) load_store_elimination (after)
  /// CHECK-DAG: <<Const1:i\d+>> IntConstant 1
  /// CHECK-DAG: <<Const2:i\d+>> IntConstant 2
  /// CHECK-DAG: <<Const3:i\d+>> IntConstant 3
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG: <<Phi:i\d+>>    Phi [<<Arg1:i\d+>>,<<Arg2:i\d+>>]
  /// CHECK-DAG:                 Return [<<Phi>>]
  /// CHECK-DAG: <<Sub:i\d+>>    Sub [<<Const3>>,<<Phi>>]
  /// CHECK-DAG:                 Return [<<Sub>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>"]) == set(["<<Const1>>","<<Const2>>"])

  /// CHECK-START: int Main.testLocalArrayMerge6(int[], boolean, boolean) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  /// CHECK-START: int Main.testLocalArrayMerge6(int[], boolean, boolean) load_store_elimination (after)
  /// CHECK-NOT:                 ArrayGet

  // Test that we create a single Phi for eliminating two loads in different blocks.
  private static int testLocalArrayMerge6(int[] a, boolean x, boolean y) {
    a[0] = 0;
    if (x) {
      a[0] = 1;
    } else {
      a[0] = 2;
    }
    // Phi for load elimination is created here.
    if (y) {
      return a[0];
    } else {
      return 3 - a[0];
    }
  }

  /// CHECK-START: int Main.testLocalArrayMerge7(int[], boolean, boolean) load_store_elimination (before)
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLocalArrayMerge7(int[], boolean, boolean) load_store_elimination (after)
  /// CHECK-DAG: <<Const0:i\d+>> IntConstant 0
  /// CHECK-DAG: <<Const1:i\d+>> IntConstant 1
  /// CHECK-DAG: <<Const2:i\d+>> IntConstant 2
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 Return [<<Phi2:i\d+>>]
  /// CHECK-DAG: <<Phi2>>        Phi [<<Arg3:i\d+>>,<<Arg4:i\d+>>]
  /// CHECK-DAG: <<Phi1:i\d+>>   Phi [<<Arg1:i\d+>>,<<Arg2:i\d+>>]
  /// CHECK-EVAL: set(["<<Arg1>>","<<Arg2>>"]) == set(["<<Const1>>","<<Const2>>"])
  /// CHECK-EVAL: set(["<<Arg3>>","<<Arg4>>"]) == set(["<<Const0>>","<<Phi1>>"])

  /// CHECK-START: int Main.testLocalArrayMerge7(int[], boolean, boolean) load_store_elimination (after)
  /// CHECK-NOT:                 ArrayGet

  // Test Phi creation for load elimination.
  private static int testLocalArrayMerge7(int[] a, boolean x, boolean y) {
    a[1] = 0;
    if (x) {
      if (y) {
        a[0] = 1;
      } else {
        a[0] = 2;
      }
      a[1] = a[0];
    }
    return a[1];
  }

  /// CHECK-START: int Main.testLocalArrayMerge8(boolean) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLocalArrayMerge8(boolean) load_store_elimination (after)
  /// CHECK-NOT:                 NewArray
  /// CHECK-NOT:                 ArraySet
  /// CHECK-NOT:                 ArrayGet

  // Test Merging default value and an identical value.
  private static int testLocalArrayMerge8(boolean x) {
    int[] a = new int[2];
    if (x) {
      a[0] = 1;  // Make sure the store below is not eliminated immediately as
                 // storing the same value already present in the heap location.
      a[0] = 0;  // Store the same value as default value to test merging with
                 // the default value from else-block.
    } else {
      // Do the same as then-block for a different heap location to avoid
      // relying on block ordering. (Test both `default+0` and `0+default`.)
      a[1] = 1;
      a[1] = 0;
    }
    return a[0] + a[1];
  }

  /// CHECK-START: void Main.$noinline$testThrowingArraySet(java.lang.Object[], java.lang.Object) load_store_elimination (before)
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet

  /// CHECK-START: void Main.$noinline$testThrowingArraySet(java.lang.Object[], java.lang.Object) load_store_elimination (after)
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet
  private static void $noinline$testThrowingArraySet(Object[] a, Object o) {
    Object olda0 = a[0];
    a[0] = null;
    a[1] = olda0;
    a[0] = o;
    a[1] = null;
  }

  /// CHECK-START: int Main.testLoop1(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 Phi

  /// CHECK-START: int Main.testLoop1(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi

  /// CHECK-START: int Main.testLoop1(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  // Test Phi creation for load elimination with loop.
  private static int testLoop1(TestClass obj, int n) {
    obj.i = 0;
    for (int i = 0; i < n; ++i) {
      obj.i = i;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop2(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 Phi

  /// CHECK-START: int Main.testLoop2(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop2(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop2(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test that we do not create any Phis for load elimination when
  // the heap value was not modified in the loop.
  private static int testLoop2(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      obj.j = i;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop3(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop3(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldSet
  /// CHECK-NOT:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop3(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  // Test elimination of a store in the loop that stores the same value that was already
  // stored before the loop and eliminating the load of that value after the loop.
  private static int testLoop3(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      obj.i = 1;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop4(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop4(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldSet
  /// CHECK-NOT:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop4(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  // Test store elimination in the loop that stores the same value that was already
  // stored before the loop, without any loads of that value.
  private static int testLoop4(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      obj.i = 1;
    }
    return n;
  }

  /// CHECK-START: int Main.testLoop5(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop5(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldSet
  /// CHECK:                     InstanceFieldSet
  /// CHECK-NOT:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop5(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  // Test eliminating loads and stores that just shuffle the same value between
  // different heap locations.
  private static int testLoop5(TestClass obj, int n) {
    // Initialize both `obj.i` and `obj.j` to the same value and then swap these values
    // in the loop. We should be able to determine that the values are always the same.
    obj.i = n;
    obj.j = n;
    for (int i = 0; i < n; ++i) {
      if ((i & 1) != 0) {
        int tmp = obj.i;
        obj.i = obj.j;
        obj.j = tmp;
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop6(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop6(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldSet
  /// CHECK:                     InstanceFieldSet
  /// CHECK-NOT:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop6(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  // Test eliminating loads and stores that just shuffle the same value between
  // different heap locations, or store the same value.
  private static int testLoop6(TestClass obj, int n) {
    // Initialize both `obj.i` and `obj.j` to the same value and then swap these values
    // in the loop or set `obj.i` to the same value. We should be able to determine
    // that the values are always the same.
    obj.i = n;
    obj.j = n;
    for (int i = 0; i < n; ++i) {
      if ((i & 1) != 0) {
        int tmp = obj.i;
        obj.i = obj.j;
        obj.j = tmp;
      } else {
        obj.i = n;
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop7(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewInstance
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop7(int) load_store_elimination (after)
  /// CHECK-NOT:                 NewInstance
  /// CHECK-NOT:                 InstanceFieldSet
  /// CHECK-NOT:                 InstanceFieldGet

  // Test eliminating loads and stores that just shuffle the default value between
  // different heap locations, or store the same value.
  private static int testLoop7(int n) {
    // Leave both `obj.i` and `obj.j` initialized to the default value and then
    // swap these values in the loop or set some to the identical value 0.
    // We should be able to determine that the values are always the same.
    TestClass obj = new TestClass();
    for (int i = 0; i < n; ++i) {
      if ((i & 1) != 0) {
        int tmp = obj.i;
        obj.i = obj.j;
        obj.j = tmp;
      } else {
        obj.i = 0;
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop8(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewInstance
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop8(int) load_store_elimination (after)
  /// CHECK-NOT:                 NewInstance
  /// CHECK-NOT:                 InstanceFieldSet
  /// CHECK-NOT:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop8(int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test eliminating loads and stores that just shuffle the same value between
  // different heap locations, or store the same value. The value is loaded
  // after conditionally setting a different value after the loop to test that
  // this does not cause creation of excessive Phis.
  private static int testLoop8(int n) {
    // Leave both `obj.i` and `obj.j` initialized to the default value and then
    // swap these values in the loop or set some to the identical value 0.
    // We should be able to determine that the values are always the same.
    TestClass obj = new TestClass();
    for (int i = 0; i < n; ++i) {
      if ((i & 1) != 0) {
        int tmp = obj.i;
        obj.i = obj.j;
        obj.j = tmp;
      } else {
        obj.i = 0;
      }
    }
    // Up to this point, `obj.i` is always 0 but the Phi placeholder below
    // must not be included in that determination despite using lazy search
    // for Phi placeholders triggered by the `obj.i` load below.
    if ((n & 1) == 0) {
      obj.i = 1;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop9(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 NewInstance
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InvokeStaticOrDirect
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop9(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi

  /// CHECK-START: int Main.testLoop9(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldSet
  /// CHECK:                     InstanceFieldSet
  /// CHECK-NOT:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop9(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 NewInstance

  /// CHECK-START: int Main.testLoop9(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldGet
  /// CHECK-NOT:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop9(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test that unknown value flowing through a loop back-edge prevents
  // elimination of a load but that load can be used as an input to a Phi
  // created to eliminate another load.
  private static int testLoop9(TestClass obj, int n) {
    TestClass obj0 = new TestClass();
    // Initialize both `obj.i` and `obj0.i` to the same value and then swap these values
    // in the loop or clobber `obj.i`. We should determine that the `obj.i` load in the
    // loop must be kept but the `obj0.i` load can be replaced by a Phi chain.
    obj0.i = n;
    obj.i = n;
    for (int i = 0; i < n; ++i) {
      if ((i & 1) != 0) {
        int tmp = obj0.i;
        obj0.i = obj.i;  // Load cannot be eliminated.
        obj.i = tmp;
      } else {
        $noinline$clobberObservables();  // Makes obj.i unknown.
      }
    }
    return obj0.i;
  }

  /// CHECK-START: int Main.testLoop10(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop10(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop10(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldGet
  /// CHECK-NOT:                 InstanceFieldGet

  // Test load elimination after finding a non-eliminated load depending
  // on loop Phi placeholder.
  private static int testLoop10(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      $noinline$clobberObservables();
    }
    int i1 = obj.i;
    obj.j = 2;  // Use write side effects to stop GVN from eliminating the load below.
    int i2 = obj.i;
    return i1 + i2;
  }

  /// CHECK-START: int Main.testLoop11(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop11(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi

  /// CHECK-START: int Main.testLoop11(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  /// CHECK-START: int Main.testLoop11(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  // Test load elimination creating two Phis that depend on each other.
  private static int testLoop11(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      if ((i & 1) != 0) {
        obj.i = 2;
      } else {
        obj.i = 3;
      }
      // There shall be a Phi created here for `obj.i` before the "++i".
      // This Phi and the loop Phi that shall be created for `obj.i` depend on each other.
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop12(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop12(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testLoop12(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  /// CHECK-START: int Main.testLoop12(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  // Test load elimination creating a single Phi with more than 2 inputs.
  private static int testLoop12(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ) {
      // Do the loop variable increment first, so that there are back-edges
      // directly from the "then" and "else" blocks below.
      ++i;
      if ((i & 1) != 0) {
        obj.i = 2;
      } else {
        obj.i = 3;
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop13(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLoop13(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop13(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 NewArray
  /// CHECK-NOT:                 ArrayGet
  /// CHECK-NOT:                 ArraySet

  /// CHECK-START: int Main.testLoop13(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test eliminating array allocation, loads and stores and creating loop Phis.
  private static int testLoop13(TestClass obj, int n) {
    int[] a = new int[3];
    for (int i = 0; i < n; ++i) {
      a[0] = a[1];
      a[1] = a[2];
      a[2] = obj.i;
    }
    return a[0];
  }

  /// CHECK-START: int Main.testLoop14(TestClass2, int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.i
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 InstanceFieldGet field_name:TestClass2.i
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.j
  /// CHECK-DAG:                 InstanceFieldGet field_name:TestClass2.i
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.k
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.j
  /// CHECK-DAG:                 InstanceFieldGet field_name:TestClass2.i
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.k
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLoop14(TestClass2, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.i
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet field_name:TestClass2.i
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.j
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.k
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.j
  /// CHECK-DAG:                 InstanceFieldSet field_name:TestClass2.k

  /// CHECK-START: int Main.testLoop14(TestClass2, int) load_store_elimination (after)
  /// CHECK-NOT:                 NewArray

  /// CHECK-START: int Main.testLoop14(TestClass2, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldGet field_name:TestClass2.i
  /// CHECK-NOT:                 InstanceFieldGet field_name:TestClass2.i

  /// CHECK-START: int Main.testLoop14(TestClass2, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test load elimination in a loop after determing that the first field load
  // (depending on loop Phi placeholder) cannot be eliminated.
  private static int testLoop14(TestClass2 obj, int n) {
    int[] a = new int[3];
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      a[0] = a[1];
      a[1] = a[2];
      int i1 = obj.i;
      obj.j = 2;  // Use write side effects to stop GVN from eliminating the load below.
      int i2 = obj.i;
      a[2] = i1;
      if ((i & 2) != 0) {
        obj.k = i2;
      } else {
        obj.j = 3;  // Use write side effects to stop GVN from eliminating the load below.
        obj.k = obj.i;
        $noinline$clobberObservables();  // Make obj.i unknown.
      }
    }
    return a[0];
  }

  /// CHECK-START: int Main.testLoop15(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-IF:     hasIsaFeature("sve")
  //
  ///     CHECK-DAG:                 VecPredWhile
  ///     CHECK-DAG:                 VecStore
  //
  /// CHECK-ELSE:
  //
  ///     CHECK-DAG:                 ArraySet
  //
  /// CHECK-FI:
  //
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLoop15(int) load_store_elimination (after)
  /// CHECK-DAG:                 NewArray
  /// CHECK-IF:     hasIsaFeature("sve")
  //
  ///     CHECK-DAG:                 VecPredWhile
  ///     CHECK-DAG:                 VecStore
  //
  /// CHECK-ELSE:
  //
  ///     CHECK-DAG:                 ArraySet
  //
  /// CHECK-FI:
  //
  /// CHECK-DAG:                 ArrayGet
  // Test that aliasing array store in the loop is not eliminated
  // when a loop Phi placeholder is marked for keeping.
  private static int testLoop15(int n) {
    int[] a = new int[n + 1];
    for (int i = 0; i < n; ++i) {
      a[i] = 1;  // Cannot be eliminated due to aliasing.
    }
    return a[0];
  }

  /// CHECK-START: int Main.testLoop16(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop16(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop16(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop16(TestClass, int) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  // Test that we match an existing loop Phi for eliminating a load.
  static int testLoop16(TestClass obj, int n) {
    obj.i = 0;
    for (int i = 0; i < n; ) {
      ++i;
      obj.i = i;
    }
    // The load is replaced by the existing Phi instead of constructing a new one.
    return obj.i;
  }

  /// CHECK-START: int Main.testLoop17(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop17(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.testLoop17(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop17(TestClass, int) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  // Test that we match an existing non-loop Phi for eliminating a load,
  // one input of the Phi being invariant across a preceding loop.
  static int testLoop17(TestClass obj, int n) {
    obj.i = 1;
    int phi = 1;
    for (int i = 0; i < n; ++i) {
      obj.j = 2;  // Unrelated.
    }
    if ((n & 1) != 0) {
      obj.i = 2;
      phi = 2;
    }
    // The load is replaced by the existing Phi instead of constructing a new one.
    return obj.i + phi;
  }

  /// CHECK-START: int Main.testLoop18(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     NewArray
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     ArrayGet
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop18(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     NewArray
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop18(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     ArrayGet

  // Test eliminating a load of the default value in a loop
  // with the array index being defined inside the loop.
  static int testLoop18(TestClass obj, int n) {
    // The NewArray is kept as it may throw for negative n.
    // TODO: Eliminate constructor fence even though the NewArray is kept.
    int[] a0 = new int[n];
    for (int i = 0; i < n; ++i) {
      obj.i = a0[i];
    }
    return n;
  }

  /// CHECK-START: int Main.testLoop19(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     NewArray
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     ArrayGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     ArraySet

  /// CHECK-START: int Main.testLoop19(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     NewArray
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop19(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     ArrayGet
  /// CHECK-NOT:                     ArraySet

  // Test eliminating a load of the default value and store of an identical value
  // in a loop with the array index being defined inside the loop.
  static int testLoop19(TestClass obj, int n) {
    // The NewArray is kept as it may throw for negative n.
    // TODO: Eliminate constructor fence even though the NewArray is kept.
    int[] a0 = new int[n];
    for (int i = 0; i < n; ++i) {
      obj.i = a0[i];
      a0[i] = 0;  // Store the same value as default.
    }
    return n;
  }

  /// CHECK-START: int Main.testLoop20(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     NewArray
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     ArrayGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     ArraySet

  /// CHECK-START: int Main.testLoop20(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     NewArray
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop20(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     ArrayGet
  /// CHECK-NOT:                     ArraySet

  // Test eliminating a load of the default value and a conditional store of an
  // identical value in a loop with the array index being defined inside the loop.
  static int testLoop20(TestClass obj, int n) {
    // The NewArray is kept as it may throw for negative n.
    // TODO: Eliminate constructor fence even though the NewArray is kept.
    int[] a0 = new int[n];
    for (int i = 0; i < n; ++i) {
      obj.i = a0[i];
      if ((i & 1) != 0) {
        a0[i] = 0;  // Store the same value as default.
      }
    }
    return n;
  }

  /// CHECK-START: int Main.testLoop21(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop21(TestClass, int) load_store_elimination (before)
  /// CHECK-NOT:                     Phi

  /// CHECK-START: int Main.testLoop21(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop21(TestClass, int) load_store_elimination (after)
  /// CHECK:                         InstanceFieldSet
  /// CHECK:                         InstanceFieldSet
  /// CHECK:                         InstanceFieldSet
  /// CHECK-NOT:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop21(TestClass, int) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  // Test load elimination when an instance field is used as the loop variable.
  static int testLoop21(TestClass obj, int n) {
    for (obj.i = 0; obj.i < n; ++obj.i) {
      obj.j = 0;  // Use write side effects to stop GVN from eliminating the load below.
      obj.j = obj.i;
    }
    return n;
  }

  /// CHECK-START: int Main.testLoop22(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop22(TestClass, int) load_store_elimination (before)
  /// CHECK-NOT:                     Phi

  /// CHECK-START: int Main.testLoop22(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop22(TestClass, int) load_store_elimination (after)
  /// CHECK:                         InstanceFieldSet
  /// CHECK:                         InstanceFieldSet
  /// CHECK-NOT:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop22(TestClass, int) load_store_elimination (after)
  /// CHECK:                         Phi
  /// CHECK-NOT:                     Phi

  // Test load and store elimination when an instance field is used as the loop
  // variable and then overwritten after the loop.
  static int testLoop22(TestClass obj, int n) {
    for (obj.i = 0; obj.i < n; ++obj.i) {
      obj.j = 0;  // Use write side effects to stop GVN from eliminating the load below.
      obj.j = obj.i;
    }
    obj.i = 0;
    return n;
  }

  /// CHECK-START: int Main.testLoop23(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop23(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop23(TestClass, int) load_store_elimination (after)
  /// CHECK:                         InstanceFieldSet
  /// CHECK-NOT:                     InstanceFieldSet

  // Test elimination of non-observable stores.
  static int testLoop23(TestClass obj, int n) {
    obj.i = -1;
    int phi = -1;
    for (int i = 0; i < n; ++i) {
      obj.i = i;
      phi = i;
    }
    if ((n & 1) != 0) {
      obj.i = 2;
      phi = 2;
    }
    obj.i = phi;  // This store shall be kept, the stores above shall be eliminated.
    return phi;
  }

  /// CHECK-START: int Main.testLoop24(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop24(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.testLoop24(TestClass, int) load_store_elimination (after)
  /// CHECK:                         InstanceFieldSet
  /// CHECK:                         InstanceFieldSet
  /// CHECK:                         InstanceFieldSet
  /// CHECK-NOT:                     InstanceFieldSet

  // Test matching Phis for store elimination.
  static int testLoop24(TestClass obj, int n) {
    obj.i = -1;
    int phi = -1;
    for (int i = 0; i < n; ++i) {
      obj.i = i;
      phi = i;
    }
    if ((n & 1) != 0) {
      obj.i = 2;
      phi = 2;
    }
    if (n == 3) {
      return -2;  // Make the above stores observable.
    }
    // As the stores above are observable and kept, we match the merged
    // heap value with existing Phis and determine that we're storing
    // the same value that's already there, so we eliminate this store.
    obj.i = phi;
    return phi;
  }

  /// CHECK-START: int Main.testLoop25(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop25(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop25(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                     InstanceFieldGet

  // Test that we do not match multiple dependent Phis for load and store elimination.
  static int testLoop25(TestClass obj, int n) {
    obj.i = 1;
    int phi = 1;
    for (int i = 0; i < n; ++i) {
      if ((i & 1) != 0) {
        obj.i = 2;
        phi = 2;
      }
      // There is a Phi here for the variable `phi` before the "++i".
      // This Phi and the loop Phi for `phi` depend on each other.
    }
    if (n == 3) {
      return -1;  // Make above stores observable.
    }
    // We're not matching multiple Phi placeholders to existing Phis. Therefore the load
    // below requires 2 extra Phis to be created and the store below shall not be eliminated
    // even though it stores the same value that's already present in the heap location.
    int tmp = obj.i;
    obj.i = phi;
    return tmp + phi;
  }

  /// CHECK-START: int Main.testLoop26(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop26(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop26(TestClass, int) load_store_elimination (after)
  /// CHECK:                         InstanceFieldGet
  /// CHECK-NOT:                     InstanceFieldGet

  // Test load elimination creating a reference Phi.
  static int testLoop26(TestClass obj, int n) {
    obj.next = new TestClass(1, 2);
    for (int i = 0; i < n; ++i) {
      obj.next = new SubTestClass();
    }
    return obj.next.i;
  }

  /// CHECK-START: int Main.testLoop27(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldGet
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop27(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     NewInstance
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldGet

  /// CHECK-START: int Main.testLoop27(TestClass, int) load_store_elimination (after)
  /// CHECK:                         InstanceFieldGet
  /// CHECK-NOT:                     InstanceFieldGet

  // Test load elimination creating two reference Phis that depend on each other.
  static int testLoop27(TestClass obj, int n) {
    obj.next = new TestClass(1, 2);
    for (int i = 0; i < n; ++i) {
      if ((i & 1) != 0) {
        obj.next = new SubTestClass();
      }
      // There shall be a Phi created here for `obj.next` before the "++i".
      // This Phi and the loop Phi that shall be created for `obj.next` depend on each other.
    }
    return obj.next.i;
  }

  /// CHECK-START: int Main.testLoop28(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLoop28(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testLoop28(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 NewArray
  /// CHECK-NOT:                 ArrayGet
  /// CHECK-NOT:                 ArraySet

  /// CHECK-START: int Main.testLoop28(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test eliminating array allocation, loads and stores and creating loop Phis
  // after determining that a field load depending on loop Phi placeholder cannot
  // be eliminated.
  private static int testLoop28(TestClass obj, int n) {
    obj.i = 1;
    int[] a = new int[3];
    for (int i = 0; i < n; ++i) {
      a[0] = a[1];
      a[1] = a[2];
      a[2] = obj.i;
      $noinline$clobberObservables();
    }
    return a[0];
  }

  /// CHECK-START: int Main.testLoop29(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet

  /// CHECK-START: int Main.testLoop29(int) load_store_elimination (after)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet

  // Test that ArraySet with non-default value prevents matching ArrayGet for
  // the same array to default value even when the ArraySet is using an index
  // offset by one, making LSA declare that the two heap locations do not alias.
  private static int testLoop29(int n) {
    int[] a = new int[4];
    int sum = 0;
    for (int i = 0; i < n; ) {
      int value = a[i] + 1;
      sum += value;
      ++i;
      a[i] = value;
    }
    return sum;
  }

  /// CHECK-START: int Main.testLoop30(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet

  /// CHECK-START: int Main.testLoop30(int) load_store_elimination (after)
  /// CHECK-NOT:                 ArrayGet
  /// CHECK-NOT:                 ArraySet

  // Test that ArraySet with default value does not prevent matching ArrayGet
  // for the same array to the default value.
  private static int testLoop30(int n) {
    int[] a = new int[4];  // NewArray is kept due to environment use by Deoptimize.
    int sum = 0;
    for (int i = 0; i < n; ) {
      int value = a[i] + 1;
      sum += value;
      ++i;
      a[i] = 0;
    }
    return sum;
  }

  /// CHECK-START: int Main.testLoop31(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet

  /// CHECK-START: int Main.testLoop31(int) load_store_elimination (after)
  /// CHECK-NOT:                 ArrayGet
  /// CHECK-NOT:                 ArraySet

  // Test that ArraySet with default value read from the array does not
  // prevent matching ArrayGet for the same array to the default value.
  private static int testLoop31(int n) {
    int[] a = new int[4];  // NewArray is kept due to environment use by Deoptimize.
    int sum = 0;
    for (int i = 0; i < n; ) {
      int value = a[i];
      sum += value;
      ++i;
      a[i] = value;
    }
    return sum;
  }

  /// CHECK-START: int Main.testLoop32(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet

  /// CHECK-START: int Main.testLoop32(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     Phi
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     InstanceFieldSet
  /// CHECK-DAG:                     Phi

  /// CHECK-START: int Main.testLoop32(TestClass, int) load_store_elimination (after)
  /// CHECK:                         InstanceFieldSet
  /// CHECK:                         InstanceFieldSet
  /// CHECK:                         InstanceFieldSet
  /// CHECK-NOT:                     InstanceFieldSet

  // Test matching Phis for store elimination.
  static int testLoop32(TestClass obj, int n) {
    obj.i = -1;
    int phi = -1;
    for (int i = 0; i < n; ) {
      ++i;
      if ((i & 1) != 0) {
        obj.i = i;
        phi = i;
      }
    }
    if ((n & 1) != 0) {
      obj.i = 2;
      phi = 2;
    }
    if (n == 3) {
      return -2;  // Make the above stores observable.
    }
    // As the stores above are observable and kept, we match the merged
    // heap value with existing Phis and determine that we're storing
    // the same value that's already there, so we eliminate this store.
    obj.i = phi;
    return phi;
  }

  // CHECK-START: int Main.testLoop33(TestClass, int) load_store_elimination (before)
  // CHECK-DAG:                     InstanceFieldSet
  // CHECK-DAG:                     NewArray
  // CHECK-DAG:                     Phi
  // CHECK-DAG:                     ArrayGet
  // CHECK-DAG:                     InstanceFieldSet
  // CHECK-DAG:                     Phi
  // CHECK-DAG:                     ArrayGet
  // CHECK-DAG:                     InstanceFieldGet
  // CHECK-DAG:                     InstanceFieldSet
  // CHECK-DAG:                     InstanceFieldGet

  // CHECK-START: int Main.testLoop33(TestClass, int) load_store_elimination (after)
  // CHECK-DAG:                     InstanceFieldSet
  // CHECK-DAG:                     Phi
  // CHECK-DAG:                     InstanceFieldSet
  // CHECK-DAG:                     Phi
  // CHECK-DAG:                     InstanceFieldGet
  // CHECK-DAG:                     InstanceFieldSet
  // CHECK-DAG:                     InstanceFieldGet

  // CHECK-START: int Main.testLoop33(TestClass, int) load_store_elimination (after)
  // CHECK-NOT:                     ArrayGet

  // Test that when processing Phi placeholder with unknown input, we allow materialized
  // default value in pre-header for array location with index defined in the loop.
  static int testLoop33(TestClass obj, int n) {
    obj.i = 0;
    int[] a0 = new int[n];
    for (int i = 0; i < n; ++i) {
      obj.i = a0[i];
      $noinline$clobberObservables();  // Make `obj.i` unknown.
    }
    for (int i = 0; i < n; ++i) {
      int zero = a0[i];
      int unknown = obj.i;
      obj.j += zero + unknown;
    }
    return obj.j;
  }

  /// CHECK-START: int Main.testLoop34(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet

  /// CHECK-START: int Main.testLoop34(int) load_store_elimination (after)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet

  // Test that ArraySet with non-default value prevents matching ArrayGet for
  // the same array to default value even when the ArraySet is using an index
  // offset by one, making LSA declare that the two heap locations do not alias.
  // Also test that the ArraySet is not eliminated.
  private static int testLoop34(int n) {
    int[] a = new int[n + 1];
    int sum = 0;
    for (int i = 0; i < n; ) {
      int value = a[i] + 1;
      sum += value;
      ++i;
      a[i] = value;
    }
    return sum;
  }

  /// CHECK-START: int Main.testLoop35(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArraySet

  /// CHECK-START: int Main.testLoop35(int) load_store_elimination (after)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet

  /// CHECK-START: int Main.testLoop35(int) load_store_elimination (after)
  /// CHECK:                     ArraySet
  /// CHECK-NOT:                 ArraySet

  // Test that ArraySet with non-default value prevents matching ArrayGet for
  // the same array to default value even when the ArraySet is using an index
  // offset by one, making LSA declare that the two heap locations do not alias.
  // Also test that the ArraySet is not eliminated and that a store after the
  // loop is eliminated.
  private static int testLoop35(int n) {
    int[] a = new int[n + 1];
    int sum = 0;
    for (int i = 0; i < n; ) {
      int value = a[i] + 1;
      sum += value;
      ++i;
      a[i] = value;
    }
    a[0] = 1;
    return sum;
  }

  /// CHECK-START: int Main.testLoop36(int) load_store_elimination (before)
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 Deoptimize
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLoop36(int) load_store_elimination (before)
  /// CHECK-NOT:                 BoundsCheck

  /// CHECK-START: int Main.testLoop36(int) load_store_elimination (after)
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 Deoptimize
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet

  // Regression test for b/187487955.
  // We previously failed a DCHECK() during the search for kept stores when
  // we encountered two array locations for the same array and considered
  // non-aliasing by LSA when only one of the array locations had index
  // defined inside the loop. Note that this situation requires that BCE
  // eliminates BoundsCheck instructions, otherwise LSA considers those
  // locations aliasing.
  private static int testLoop36(int n) {
    int[] a = new int[n];
    int zero = 0;
    int i = 0;
    for (; i < n; ++i) {
      a[i] = i;
      // Extra instructions to avoid loop unrolling.
      zero = (((zero ^ 1) + 2) ^ 1) - 2;
      zero = (((zero ^ 4) + 8) ^ 4) - 8;
    }
    // Use 4 loads with consecutive fixed offsets from the loop Phi for `i`.
    // BCE shall replace BoundsChecks with Deoptimize, so that indexes here are
    // the Phi plus/minus a constant, something that LSA considers non-aliasing
    // with the Phi (LSA does not take different loop iterations into account)
    // but LSE must consider aliasing across dfferent loop iterations.
    return a[i - 1] + a[i - 2] + a[i - 3] + a[i - 4] + zero;
  }

  /// CHECK-START: int Main.testLoop37(int) load_store_elimination (before)
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 Deoptimize
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLoop37(int) load_store_elimination (before)
  /// CHECK-NOT:                 BoundsCheck

  /// CHECK-START: int Main.testLoop37(int) load_store_elimination (after)
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 Deoptimize
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet

  // Similar to testLoop36 but the writes are done via a different reference to the same array.
  // We previously used a reference comparison for back-edge aliasing analysis but this test
  // has different references and therefore needs `HeapLocationCollector::CanReferencesAlias()`.
  private static int testLoop37(int n) {
    int[] a = new int[n];
    int[] b = $noinline$returnArg(a);
    int zero = 0;
    int i = 0;
    for (; i < n; ++i) {
      b[i] = i;
      // Extra instructions to avoid loop unrolling.
      zero = (((zero ^ 1) + 2) ^ 1) - 2;
      zero = (((zero ^ 4) + 8) ^ 4) - 8;
    }
    // Use 4 loads with consecutive fixed offsets from the loop Phi for `i`.
    // BCE shall replace BoundsChecks with Deoptimize, so that indexes here are
    // the Phi plus/minus a constant, something that LSA considers non-aliasing
    // with the Phi (LSA does not take different loop iterations into account)
    // but LSE must consider aliasing across dfferent loop iterations.
    return a[i - 1] + a[i - 2] + a[i - 3] + a[i - 4] + zero;
  }

  private static int[] $noinline$returnArg(int[] a) {
    return a;
  }

  /// CHECK-START: int Main.testLoop38(int, int[]) load_store_elimination (before)
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 Deoptimize
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet

  /// CHECK-START: int Main.testLoop38(int, int[]) load_store_elimination (before)
  /// CHECK-NOT:                 BoundsCheck

  /// CHECK-START: int Main.testLoop38(int, int[]) load_store_elimination (after)
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 Deoptimize

  /// CHECK-START: int Main.testLoop38(int, int[]) load_store_elimination (after)
  /// CHECK-NOT:                 ArrayGet

  // Similar to testLoop37 but writing to a different array that exists before allocating `a`,
  // so that `HeapLocationCollector::CanReferencesAlias()` returns false and all the ArrayGet
  // instructions are actually eliminated.
  private static int testLoop38(int n, int[] b) {
    int[] a = new int[n];
    int zero = 0;
    int i = 0;
    for (; i < n; ++i) {
      b[i] = i;
      // Extra instructions to avoid loop unrolling.
      zero = (((zero ^ 1) + 2) ^ 1) - 2;
      zero = (((zero ^ 4) + 8) ^ 4) - 8;
    }
    // Use 4 loads with consecutive fixed offsets from the loop Phi for `i`.
    // BCE shall replace BoundsChecks with Deoptimize, so that indexes here are
    // the Phi plus/minus a constant, something that LSA considers non-aliasing
    // with the Phi (LSA does not take different loop iterations into account)
    // but LSE must consider aliasing across dfferent loop iterations.
    return a[i - 1] + a[i - 2] + a[i - 3] + a[i - 4] + zero;
  }

  /// CHECK-START: int Main.testNestedLoop1(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop1(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  // Test heap value clobbering in nested loop.
  private static int testNestedLoop1(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        $noinline$clobberObservables();
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testNestedLoop2(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop2(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testNestedLoop2(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldGet
  /// CHECK-NOT:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop2(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test heap value clobbering in the nested loop and load elimination for a heap
  // location then set to known value before the end of the outer loop.
  private static int testNestedLoop2(TestClass obj, int n) {
    obj.i = 1;
    obj.j = 2;
    for (int i = 0; i < n; ++i) {
      int tmp = obj.j;
      for (int j = i + 1; j < n; ++j) {
        $noinline$clobberObservables();
      }
      obj.i = tmp;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testNestedLoop3(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop3(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testNestedLoop3(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldGet
  /// CHECK-NOT:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop3(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test heap value clobbering in the nested loop and load elimination for a heap
  // location then set to known value before the end of the outer loop.
  private static int testNestedLoop3(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      obj.j = 2;
      for (int j = i + 1; j < n; ++j) {
        $noinline$clobberObservables();
      }
      obj.i = obj.j;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testNestedLoop4(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop4(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testNestedLoop4(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop4(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test creating loop Phis for both inner and outer loop to eliminate a load.
  private static int testNestedLoop4(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        obj.i = 2;
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testNestedLoop5(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop5(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testNestedLoop5(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop5(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test creating a loop Phi for outer loop to eliminate a load.
  private static int testNestedLoop5(TestClass obj, int n) {
    obj.i = 1;
    for (int i = 0; i < n; ++i) {
      obj.i = 2;
      for (int j = i + 1; j < n; ++j) {
        obj.j = 3;  // Unrelated.
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testNestedLoop6(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop6(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testNestedLoop6(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldGet
  /// CHECK-NOT:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop6(TestClass, int) load_store_elimination (after)
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK:                     Phi
  /// CHECK-NOT:                 Phi

  // Test heap value clobbering in the nested loop and load elimination for a heap
  // location then set to known value before the end of that inner loop.
  private static int testNestedLoop6(TestClass obj, int n) {
    obj.i = 1;
    obj.j = 2;
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        int tmp = obj.j;
        $noinline$clobberObservables();
        obj.i = tmp;
      }
    }
    return obj.i;
  }

  /// CHECK-START: int Main.testNestedLoop7(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testNestedLoop7(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 InstanceFieldSet

  /// CHECK-START: int Main.testNestedLoop7(TestClass, int) load_store_elimination (after)
  /// CHECK-NOT:                 ArrayGet

  // Test load elimination in inner loop reading default value that is loop invariant
  // with an index defined inside the inner loop.
  private static int testNestedLoop7(TestClass obj, int n) {
    // The NewArray is kept as it may throw for negative n.
    // TODO: Eliminate constructor fence even though the NewArray is kept.
    int[] a0 = new int[n];
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        obj.i = a0[j];
      }
    }
    return n;
  }

  /// CHECK-START: int Main.testNestedLoop8(TestClass, int) load_store_elimination (before)
  /// CHECK-DAG:                 NewInstance
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 NewInstance
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop8(TestClass, int) load_store_elimination (after)
  /// CHECK-DAG:                 NewInstance
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 Phi
  /// CHECK-DAG:                 NewInstance
  /// CHECK-DAG:                 InstanceFieldSet
  /// CHECK-DAG:                 InstanceFieldGet

  /// CHECK-START: int Main.testNestedLoop8(TestClass, int) load_store_elimination (after)
  /// CHECK:                     InstanceFieldGet
  /// CHECK-NOT:                 InstanceFieldGet

  // Test reference type propagation for Phis created for outer and inner loop.
  private static int testNestedLoop8(TestClass obj, int n) {
    obj.next = new SubTestClass();
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        obj.next = new TestClass();
      }
    }
    // The Phis created in both loop headers for replacing `obj.next` depend on each other.
    return obj.next.i;
  }


  /// CHECK-START: long Main.testOverlapLoop(int) load_store_elimination (before)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 If
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 Goto

  /// CHECK-START: long Main.testOverlapLoop(int) load_store_elimination (after)
  /// CHECK-DAG:                 NewArray
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 If
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArrayGet
  /// CHECK-DAG:                 ArraySet
  /// CHECK-DAG:                 Goto
  /// CHECK-NOT:                 ArrayGet

  // Test that we don't incorrectly remove writes needed by later loop iterations
  // NB This is fibonacci numbers
  private static long testOverlapLoop(int cnt) {
    long[] w = new long[cnt];
    w[1] = 1;
    long t = 1;
    for (int i = 2; i < cnt; ++i) {
      w[i] = w[i - 1] + w[i - 2];
      t = w[i];
    }
    return t;
  }

  private static boolean $noinline$getBoolean(boolean val) {
    return val;
  }

  /// CHECK-START: int Main.$noinline$testPartialEscape1(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:     ParameterValue
  /// CHECK-DAG:     NewInstance
  /// CHECK-DAG:     InvokeStaticOrDirect
  /// CHECK-DAG:     InstanceFieldSet
  /// CHECK-DAG:     InvokeStaticOrDirect
  /// CHECK-DAG:     InstanceFieldGet
  /// CHECK-DAG:     InstanceFieldGet
  /// CHECK-DAG:     InstanceFieldSet
  /// CHECK-DAG:     InstanceFieldGet
  /// CHECK-DAG:     InstanceFieldGet
  /// CHECK-DAG:     Phi
  //
  /// CHECK-NOT:     NewInstance
  /// CHECK-NOT:     InvokeStaticOrDirect
  /// CHECK-NOT:     InstanceFieldSet
  /// CHECK-NOT:     InstanceFieldGet
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape1(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG:     ParameterValue
  /// CHECK-DAG:     NewInstance
  /// CHECK-DAG:     Phi
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape1(TestClass, boolean) load_store_elimination (after)
  /// CHECK:         InvokeStaticOrDirect
  /// CHECK:         InvokeStaticOrDirect
  //
  /// CHECK-NOT:     InvokeStaticOrDirect

  /// CHECK-START: int Main.$noinline$testPartialEscape1(TestClass, boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet
  //
  /// CHECK-NOT:     InstanceFieldSet
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape1(TestClass, boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldGet
  /// CHECK:         InstanceFieldGet
  /// CHECK:         InstanceFieldGet
  //
  /// CHECK-NOT:     InstanceFieldGet
  private static int $noinline$testPartialEscape1(TestClass obj, boolean escape) {
    TestClass i = new SubTestClass();
    int res;
    if ($noinline$getBoolean(escape)) {
      i.next = obj;
      $noinline$Escape(i);
      res = i.next.i;
    } else {
      i.next = obj;
      res = i.next.i;
    }
    return res;
  }

  /// CHECK-START: int Main.$noinline$testPartialEscape2(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG:     ParameterValue
  /// CHECK-DAG:     NewInstance
  /// CHECK-DAG:     InvokeStaticOrDirect
  /// CHECK-DAG:     InvokeStaticOrDirect
  /// CHECK-DAG:     InvokeStaticOrDirect
  /// CHECK-DAG:     InstanceFieldSet
  /// CHECK-DAG:     InstanceFieldSet
  /// CHECK-DAG:     InstanceFieldSet
  /// CHECK-DAG:     InstanceFieldGet
  /// CHECK-DAG:     InstanceFieldGet
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape2(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG:     ParameterValue
  /// CHECK-DAG:     NewInstance
  /// CHECK-DAG:     Phi
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape2(TestClass, boolean) load_store_elimination (after)
  /// CHECK:         InvokeStaticOrDirect
  /// CHECK:         InvokeStaticOrDirect
  /// CHECK:         InvokeStaticOrDirect
  //
  /// CHECK-NOT:     InvokeStaticOrDirect

  /// CHECK-START: int Main.$noinline$testPartialEscape2(TestClass, boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet predicated:false
  /// CHECK-NOT:     InstanceFieldSet predicated:false
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape2(TestClass, boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet predicated:true
  /// CHECK-NOT:     InstanceFieldSet predicated:true
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape2(TestClass, boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldGet
  //
  /// CHECK-NOT:     InstanceFieldGet
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape2(TestClass, boolean) load_store_elimination (after)
  /// CHECK:         PredicatedInstanceFieldGet
  //
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  private static int $noinline$testPartialEscape2(TestClass obj, boolean escape) {
    TestClass i = new SubTestClass();
    if ($noinline$getBoolean(escape)) {
      i.next = obj;
      $noinline$Escape(i);
    } else {
      i.next = obj;
    }
    $noinline$clobberObservables();
    // Predicated-get
    TestClass res = i.next;
    // Predicated-set
    i.next = null;
    return res.i;
  }

  /// CHECK-START: float Main.$noinline$testPartialEscape3_float(boolean) load_store_elimination (before)
  /// CHECK-NOT:     Phi
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  //
  /// CHECK-START: float Main.$noinline$testPartialEscape3_float(boolean) load_store_elimination (after)
  /// CHECK:         Phi
  /// CHECK:         Phi
  /// CHECK-NOT:     Phi
  //
  /// CHECK-START: float Main.$noinline$testPartialEscape3_float(boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet predicated:true
  /// CHECK-NOT:     InstanceFieldSet predicated:true
  //
  /// CHECK-START: float Main.$noinline$testPartialEscape3_float(boolean) load_store_elimination (after)
  /// CHECK:         PredicatedInstanceFieldGet
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  private static float $noinline$testPartialEscape3_float(boolean escape) {
    TestClass4 tc = new TestClass4();
    if ($noinline$getBoolean(escape)) {
      $noinline$Escape4(tc);
    } else {
      tc.floatField -= 1f;
    }
    // Partial escape
    $noinline$clobberObservables();
    // Predicated set
    tc.floatField *= 10;
    // Predicated get
    return tc.floatField;
  }

  /// CHECK-START: double Main.$noinline$testPartialEscape3_double(boolean) load_store_elimination (before)
  /// CHECK-NOT:     Phi
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  //
  /// CHECK-START: double Main.$noinline$testPartialEscape3_double(boolean) load_store_elimination (after)
  /// CHECK:         Phi
  /// CHECK:         Phi
  /// CHECK-NOT:     Phi
  //
  /// CHECK-START: double Main.$noinline$testPartialEscape3_double(boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet predicated:true
  /// CHECK-NOT:     InstanceFieldSet predicated:true
  //
  /// CHECK-START: double Main.$noinline$testPartialEscape3_double(boolean) load_store_elimination (after)
  /// CHECK:         PredicatedInstanceFieldGet
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  private static double $noinline$testPartialEscape3_double(boolean escape) {
    TestClass4 tc = new TestClass4();
    if ($noinline$getBoolean(escape)) {
      $noinline$Escape4(tc);
    } else {
      tc.doubleField -= 1d;
    }
    // Partial escape
    $noinline$clobberObservables();
    // Predicated set
    tc.doubleField *= 10;
    // Predicated get
    return tc.doubleField;
  }

  /// CHECK-START: short Main.$noinline$testPartialEscape3_short(boolean) load_store_elimination (before)
  /// CHECK-NOT:     Phi
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  //
  /// CHECK-START: short Main.$noinline$testPartialEscape3_short(boolean) load_store_elimination (after)
  /// CHECK:         Phi
  /// CHECK:         Phi
  /// CHECK-NOT:     Phi
  //
  /// CHECK-START: short Main.$noinline$testPartialEscape3_short(boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet predicated:true
  /// CHECK-NOT:     InstanceFieldSet predicated:true
  //
  /// CHECK-START: short Main.$noinline$testPartialEscape3_short(boolean) load_store_elimination (after)
  /// CHECK:         PredicatedInstanceFieldGet
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  private static short $noinline$testPartialEscape3_short(boolean escape) {
    TestClass4 tc = new TestClass4();
    if ($noinline$getBoolean(escape)) {
      $noinline$Escape4(tc);
    } else {
      tc.shortField -= 1;
    }
    // Partial escape
    $noinline$clobberObservables();
    // Predicated set
    tc.shortField *= 10;
    // Predicated get
    return tc.shortField;
  }

  /// CHECK-START: byte Main.$noinline$testPartialEscape3_byte(boolean) load_store_elimination (before)
  /// CHECK-NOT:     Phi
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  //
  /// CHECK-START: byte Main.$noinline$testPartialEscape3_byte(boolean) load_store_elimination (after)
  /// CHECK:         Phi
  /// CHECK:         Phi
  /// CHECK-NOT:     Phi
  //
  /// CHECK-START: byte Main.$noinline$testPartialEscape3_byte(boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet predicated:true
  /// CHECK-NOT:     InstanceFieldSet predicated:true
  //
  /// CHECK-START: byte Main.$noinline$testPartialEscape3_byte(boolean) load_store_elimination (after)
  /// CHECK:         PredicatedInstanceFieldGet
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  private static byte $noinline$testPartialEscape3_byte(boolean escape) {
    TestClass4 tc = new TestClass4();
    if ($noinline$getBoolean(escape)) {
      $noinline$Escape4(tc);
    } else {
      tc.byteField -= 1;
    }
    // Partial escape
    $noinline$clobberObservables();
    // Predicated set
    tc.byteField *= 10;
    // Predicated get
    return tc.byteField;
  }

  /// CHECK-START: int Main.$noinline$testPartialEscape3_int(boolean) load_store_elimination (before)
  /// CHECK-NOT:     Phi
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape3_int(boolean) load_store_elimination (after)
  /// CHECK:         Phi
  /// CHECK:         Phi
  /// CHECK-NOT:     Phi
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape3_int(boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet predicated:true
  /// CHECK-NOT:     InstanceFieldSet predicated:true
  //
  /// CHECK-START: int Main.$noinline$testPartialEscape3_int(boolean) load_store_elimination (after)
  /// CHECK:         PredicatedInstanceFieldGet
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  private static int $noinline$testPartialEscape3_int(boolean escape) {
    TestClass4 tc = new TestClass4();
    if ($noinline$getBoolean(escape)) {
      $noinline$Escape4(tc);
    } else {
      tc.intField -= 1;
    }
    // Partial escape
    $noinline$clobberObservables();
    // Predicated set
    tc.intField *= 10;
    // Predicated get
    return tc.intField;
  }

  /// CHECK-START: long Main.$noinline$testPartialEscape3_long(boolean) load_store_elimination (before)
  /// CHECK-NOT:     Phi
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  //
  /// CHECK-START: long Main.$noinline$testPartialEscape3_long(boolean) load_store_elimination (after)
  /// CHECK:         Phi
  /// CHECK:         Phi
  /// CHECK-NOT:     Phi
  //
  /// CHECK-START: long Main.$noinline$testPartialEscape3_long(boolean) load_store_elimination (after)
  /// CHECK:         InstanceFieldSet predicated:true
  /// CHECK-NOT:     InstanceFieldSet predicated:true
  //
  /// CHECK-START: long Main.$noinline$testPartialEscape3_long(boolean) load_store_elimination (after)
  /// CHECK:         PredicatedInstanceFieldGet
  /// CHECK-NOT:     PredicatedInstanceFieldGet
  private static long $noinline$testPartialEscape3_long(boolean escape) {
    TestClass4 tc = new TestClass4();
    if ($noinline$getBoolean(escape)) {
      $noinline$Escape4(tc);
    } else {
      tc.longField -= 1;
    }
    // Partial escape
    $noinline$clobberObservables();
    // Predicated set
    tc.longField *= 10;
    // Predicated get
    return tc.longField;
  }

  private static void $noinline$clobberObservables() {}

  static void assertLongEquals(long result, long expected) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  static void assertIntEquals(int result, int expected) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  static void assertFloatEquals(float result, float expected) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  static void assertDoubleEquals(double result, double expected) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void main(String[] args) {
    assertDoubleEquals(Math.PI * Math.PI * Math.PI, calcCircleArea(Math.PI));
    assertIntEquals(test1(new TestClass(), new TestClass()), 3);
    assertIntEquals(test2(new TestClass()), 1);
    TestClass obj1 = new TestClass();
    TestClass obj2 = new TestClass();
    obj1.next = obj2;
    assertIntEquals(test3(obj1), 10);
    assertIntEquals(test4(new TestClass(), true), 1);
    assertIntEquals(test4(new TestClass(), false), 1);
    assertIntEquals(test5(new TestClass(), true), 1);
    assertIntEquals(test5(new TestClass(), false), 2);
    assertIntEquals(test6(new TestClass(), new TestClass(), true), 4);
    assertIntEquals(test6(new TestClass(), new TestClass(), false), 2);
    assertIntEquals(test7(new TestClass()), 1);
    assertIntEquals(test8(), 1);
    obj1 = new TestClass();
    obj2 = new TestClass();
    obj1.next = obj2;
    assertIntEquals(test9(new TestClass()), 1);
    assertIntEquals(test10(new TestClass(3, 4)), 3);
    assertIntEquals(TestClass.si, 3);
    assertIntEquals(test11(new TestClass()), 10);
    assertIntEquals(test12(new TestClass(), new TestClass()), 10);
    assertIntEquals(test13(new TestClass(), new TestClass2()), 3);
    SubTestClass obj3 = new SubTestClass();
    assertIntEquals(test14(obj3, obj3), 2);
    assertIntEquals(test15(), 2);
    assertIntEquals(test16(), 3);
    assertIntEquals(test17(), 0);
    assertIntEquals(test18(new TestClass()), 1);
    float[] fa1 = { 0.8f };
    float[] fa2 = { 1.8f };
    assertFloatEquals(test19(fa1, fa2), 1.8f);
    assertFloatEquals(test20().i, 0);
    test21(new TestClass());
    assertIntEquals(test22(), 13);
    assertIntEquals(test23(true), 4);
    assertIntEquals(test23(false), 5);
    assertFloatEquals(test24(), 8.0f);
    assertIntEquals(test25(false, true, true), 5);
    assertIntEquals(test25(true, false, true), 2);
    assertFloatEquals(test26(5), 0.0f);
    assertFloatEquals(test26(3), 1.0f);
    assertIntEquals(test27(false, true), 1);
    assertIntEquals(test27(true, false), 1);
    assertIntEquals(test28(false, true), 0);
    assertIntEquals(test28(true, true), 5);
    assertFloatEquals(test29(true), 5.0f);
    assertFloatEquals(test29(false), 2.0f);
    assertIntEquals(test30(new TestClass(), true), 1);
    assertIntEquals(test30(new TestClass(), false), 0);
    assertIntEquals(test31(true, true), 5);
    assertIntEquals(test31(true, false), 6);
    assertIntEquals(test32(1), 10);
    assertIntEquals(test32(2), 10);
    assertIntEquals(test33(new TestClass(), true), 1);
    assertIntEquals(test33(new TestClass(), false), 2);
    assertIntEquals(test34(new TestClass(), true, true), 3);
    assertIntEquals(test34(new TestClass(), false, true), 4);
    assertIntEquals(test34(new TestClass(), true, false), 1);
    assertIntEquals(test34(new TestClass(), false, false), 2);
    assertIntEquals(test35(new TestClass(), true, true), 3);
    assertIntEquals(test35(new TestClass(), false, true), 2);
    assertIntEquals(test35(new TestClass(), true, false), 1);
    assertIntEquals(test35(new TestClass(), false, false), 2);
    assertIntEquals(test36(new TestClass(), true), 2);
    assertIntEquals(test36(new TestClass(), false), 4);
    assertIntEquals(test37(new TestClass(), true), 1);
    assertIntEquals(test37(new TestClass(), false), 0);
    assertIntEquals(test38(new TestClass(), true), 1);
    assertIntEquals(test38(new TestClass(), false), 2);
    assertIntEquals(test39(new TestClass(), true), 0);
    assertIntEquals(test39(new TestClass(), false), 1);

    testFinalizableByForcingGc();
    assertIntEquals($noinline$testHSelect(true), 0xdead);
    int[] array = {2, 5, 9, -1, -3, 10, 8, 4};
    assertIntEquals(sumWithinRange(array, 1, 5), 11);
    assertFloatEquals(testAllocationEliminationWithLoops(), 1.0f);
    assertFloatEquals(mF, 0f);
    assertDoubleEquals(Math.PI * Math.PI * Math.PI, getCircleArea(Math.PI, true));
    assertDoubleEquals(0d, getCircleArea(Math.PI, false));

    assertIntEquals($noinline$testConversion1(new TestClass(), 300), 300);
    assertIntEquals($noinline$testConversion1(new TestClass(), 301), 45);
    assertIntEquals($noinline$testConversion2(new TestClass(), 300), 300);
    assertIntEquals($noinline$testConversion2(new TestClass(), 301), 90);
    assertIntEquals($noinline$testConversion3(new TestClass(), 0), 0);
    assertIntEquals($noinline$testConversion3(new TestClass(), 1), 0);
    assertIntEquals($noinline$testConversion3(new TestClass(), 128), 127);
    assertIntEquals($noinline$testConversion3(new TestClass(), 129), -128);
    assertIntEquals($noinline$testConversion4(new TestClass(), 0), 0);
    assertIntEquals($noinline$testConversion4(new TestClass(), 1), 0);
    assertIntEquals($noinline$testConversion4(new TestClass(), 128), 254);
    assertIntEquals($noinline$testConversion4(new TestClass(), 129), -256);

    int[] iarray = {0, 0, 0};
    double[] darray = {0d, 0d, 0d};
    try {
      assertDoubleEquals(Math.PI * Math.PI * Math.PI, testDeoptimize(iarray, darray, Math.PI));
    } catch (Exception e) {
      System.out.println(e.getClass().getName());
    }
    assertIntEquals(iarray[0], 1);
    assertIntEquals(iarray[1], 1);
    assertIntEquals(iarray[2], 1);
    assertDoubleEquals(darray[0], Math.PI);
    assertDoubleEquals(darray[1], Math.PI);
    assertDoubleEquals(darray[2], Math.PI);

    assertIntEquals(testAllocationEliminationOfArray1(), 11);
    assertIntEquals(testAllocationEliminationOfArray2(), 11);
    assertIntEquals(testAllocationEliminationOfArray3(2), 4);
    assertIntEquals(testAllocationEliminationOfArray4(2), 6);
    assertIntEquals(testAllocationEliminationOfArray5(2), 12);
    try {
      testAllocationEliminationOfArray5(-2);
    } catch (NegativeArraySizeException e) {
      System.out.println("Got NegativeArraySizeException.");
    }

    assertIntEquals(testStoreStore().i, 41);
    assertIntEquals(testStoreStore().j, 43);

    assertIntEquals(testExitMerge(true), 2);
    assertIntEquals(testExitMerge2(true), 2);
    assertIntEquals(testExitMerge2(false), 2);

    TestClass2 testclass2 = new TestClass2();
    testStoreStore2(testclass2);
    assertIntEquals(testclass2.i, 43);
    assertIntEquals(testclass2.j, 44);

    testStoreStore3(testclass2, true);
    assertIntEquals(testclass2.i, 41);
    assertIntEquals(testclass2.j, 43);
    testStoreStore3(testclass2, false);
    assertIntEquals(testclass2.i, 41);
    assertIntEquals(testclass2.j, 44);

    testStoreStore4();
    assertIntEquals(TestClass.si, 62);

    int ret = testStoreStore5(testclass2, testclass2);
    assertIntEquals(testclass2.i, 72);
    assertIntEquals(ret, 71);

    testclass2.j = 88;
    ret = testStoreStore6(testclass2, testclass2);
    assertIntEquals(testclass2.i, 82);
    assertIntEquals(ret, 88);

    ret = testNoSideEffects(iarray);
    assertIntEquals(iarray[0], 101);
    assertIntEquals(iarray[1], 103);
    assertIntEquals(ret, 108);

    try {
      testThrow(testclass2, new Exception());
    } catch (Exception e) {}
    assertIntEquals(testclass2.i, 55);

    assertIntEquals(testStoreStoreWithDeoptimize(new int[4]), 4);

    assertIntEquals($noinline$testByteArrayDefaultValue(), 0);

    assertIntEquals(testLocalArrayMerge1(true), 1);
    assertIntEquals(testLocalArrayMerge1(false), 1);
    assertIntEquals(testLocalArrayMerge2(true), 2);
    assertIntEquals(testLocalArrayMerge2(false), 3);
    assertIntEquals(testLocalArrayMerge3(true), 2);
    assertIntEquals(testLocalArrayMerge3(false), 1);
    assertIntEquals(testLocalArrayMerge4(true), 2);
    assertIntEquals(testLocalArrayMerge4(false), 2);
    assertIntEquals(testLocalArrayMerge5(new int[]{ 7 }, true), 7);
    assertIntEquals(testLocalArrayMerge5(new int[]{ 9 }, false), 9);
    assertIntEquals(testLocalArrayMerge6(new int[1], true, true), 1);
    assertIntEquals(testLocalArrayMerge6(new int[1], true, false), 2);
    assertIntEquals(testLocalArrayMerge6(new int[1], false, true), 2);
    assertIntEquals(testLocalArrayMerge6(new int[1], false, false), 1);
    assertIntEquals(testLocalArrayMerge7(new int[2], true, true), 1);
    assertIntEquals(testLocalArrayMerge7(new int[2], true, false), 2);
    assertIntEquals(testLocalArrayMerge7(new int[2], false, true), 0);
    assertIntEquals(testLocalArrayMerge7(new int[2], false, false), 0);
    assertIntEquals(testLocalArrayMerge8(true), 0);
    assertIntEquals(testLocalArrayMerge8(false), 0);

    TestClass[] tca = new TestClass[] { new TestClass(), null };
    try {
      $noinline$testThrowingArraySet(tca, new TestClass2());
    } catch (ArrayStoreException expected) {
      if (tca[0] != null) {
        throw new Error("tca[0] is not null");
      }
      if (tca[1] == null) {
        throw new Error("tca[1] is null");
      }
    }

    assertIntEquals(testLoop1(new TestClass(), 0), 0);
    assertIntEquals(testLoop1(new TestClass(), 1), 0);
    assertIntEquals(testLoop1(new TestClass(), 2), 1);
    assertIntEquals(testLoop1(new TestClass(), 3), 2);
    assertIntEquals(testLoop2(new TestClass(), 0), 1);
    assertIntEquals(testLoop2(new TestClass(), 1), 1);
    assertIntEquals(testLoop2(new TestClass(), 2), 1);
    assertIntEquals(testLoop2(new TestClass(), 3), 1);
    assertIntEquals(testLoop3(new TestClass(), 0), 1);
    assertIntEquals(testLoop3(new TestClass(), 1), 1);
    assertIntEquals(testLoop3(new TestClass(), 2), 1);
    assertIntEquals(testLoop3(new TestClass(), 3), 1);
    assertIntEquals(testLoop4(new TestClass(), 0), 0);
    assertIntEquals(testLoop4(new TestClass(), 1), 1);
    assertIntEquals(testLoop4(new TestClass(), 2), 2);
    assertIntEquals(testLoop4(new TestClass(), 3), 3);
    assertIntEquals(testLoop5(new TestClass(), 0), 0);
    assertIntEquals(testLoop5(new TestClass(), 1), 1);
    assertIntEquals(testLoop5(new TestClass(), 2), 2);
    assertIntEquals(testLoop5(new TestClass(), 3), 3);
    assertIntEquals(testLoop6(new TestClass(), 0), 0);
    assertIntEquals(testLoop6(new TestClass(), 1), 1);
    assertIntEquals(testLoop6(new TestClass(), 2), 2);
    assertIntEquals(testLoop6(new TestClass(), 3), 3);
    assertIntEquals(testLoop7(0), 0);
    assertIntEquals(testLoop7(1), 0);
    assertIntEquals(testLoop7(2), 0);
    assertIntEquals(testLoop7(3), 0);
    assertIntEquals(testLoop8(0), 1);
    assertIntEquals(testLoop8(1), 0);
    assertIntEquals(testLoop8(2), 1);
    assertIntEquals(testLoop8(3), 0);
    assertIntEquals(testLoop9(new TestClass(), 0), 0);
    assertIntEquals(testLoop9(new TestClass(), 1), 1);
    assertIntEquals(testLoop9(new TestClass(), 2), 2);
    assertIntEquals(testLoop9(new TestClass(), 3), 3);
    assertIntEquals(testLoop10(new TestClass(), 0), 2);
    assertIntEquals(testLoop10(new TestClass(), 1), 2);
    assertIntEquals(testLoop10(new TestClass(), 2), 2);
    assertIntEquals(testLoop10(new TestClass(), 3), 2);
    assertIntEquals(testLoop11(new TestClass(), 0), 1);
    assertIntEquals(testLoop11(new TestClass(), 1), 3);
    assertIntEquals(testLoop11(new TestClass(), 2), 2);
    assertIntEquals(testLoop11(new TestClass(), 3), 3);
    assertIntEquals(testLoop12(new TestClass(), 0), 1);
    assertIntEquals(testLoop12(new TestClass(), 1), 2);
    assertIntEquals(testLoop12(new TestClass(), 2), 3);
    assertIntEquals(testLoop12(new TestClass(), 3), 2);
    assertIntEquals(testLoop13(new TestClass(1, 2), 0), 0);
    assertIntEquals(testLoop13(new TestClass(1, 2), 1), 0);
    assertIntEquals(testLoop13(new TestClass(1, 2), 2), 0);
    assertIntEquals(testLoop13(new TestClass(1, 2), 3), 1);
    assertIntEquals(testLoop14(new TestClass2(), 0), 0);
    assertIntEquals(testLoop14(new TestClass2(), 1), 0);
    assertIntEquals(testLoop14(new TestClass2(), 2), 0);
    assertIntEquals(testLoop14(new TestClass2(), 3), 1);
    assertIntEquals(testLoop15(0), 0);
    assertIntEquals(testLoop15(1), 1);
    assertIntEquals(testLoop15(2), 1);
    assertIntEquals(testLoop15(3), 1);
    assertIntEquals(testLoop16(new TestClass(), 0), 0);
    assertIntEquals(testLoop16(new TestClass(), 1), 1);
    assertIntEquals(testLoop16(new TestClass(), 2), 2);
    assertIntEquals(testLoop16(new TestClass(), 3), 3);
    assertIntEquals(testLoop17(new TestClass(), 0), 2);
    assertIntEquals(testLoop17(new TestClass(), 1), 4);
    assertIntEquals(testLoop17(new TestClass(), 2), 2);
    assertIntEquals(testLoop17(new TestClass(), 3), 4);
    assertIntEquals(testLoop18(new TestClass(), 0), 0);
    assertIntEquals(testLoop18(new TestClass(), 1), 1);
    assertIntEquals(testLoop18(new TestClass(), 2), 2);
    assertIntEquals(testLoop18(new TestClass(), 3), 3);
    assertIntEquals(testLoop19(new TestClass(), 0), 0);
    assertIntEquals(testLoop19(new TestClass(), 1), 1);
    assertIntEquals(testLoop19(new TestClass(), 2), 2);
    assertIntEquals(testLoop19(new TestClass(), 3), 3);
    assertIntEquals(testLoop20(new TestClass(), 0), 0);
    assertIntEquals(testLoop20(new TestClass(), 1), 1);
    assertIntEquals(testLoop20(new TestClass(), 2), 2);
    assertIntEquals(testLoop20(new TestClass(), 3), 3);
    assertIntEquals(testLoop21(new TestClass(), 0), 0);
    assertIntEquals(testLoop21(new TestClass(), 1), 1);
    assertIntEquals(testLoop21(new TestClass(), 2), 2);
    assertIntEquals(testLoop21(new TestClass(), 3), 3);
    assertIntEquals(testLoop22(new TestClass(), 0), 0);
    assertIntEquals(testLoop22(new TestClass(), 1), 1);
    assertIntEquals(testLoop22(new TestClass(), 2), 2);
    assertIntEquals(testLoop22(new TestClass(), 3), 3);
    assertIntEquals(testLoop23(new TestClass(), 0), -1);
    assertIntEquals(testLoop23(new TestClass(), 1), 2);
    assertIntEquals(testLoop23(new TestClass(), 2), 1);
    assertIntEquals(testLoop23(new TestClass(), 3), 2);
    assertIntEquals(testLoop24(new TestClass(), 0), -1);
    assertIntEquals(testLoop24(new TestClass(), 1), 2);
    assertIntEquals(testLoop24(new TestClass(), 2), 1);
    assertIntEquals(testLoop24(new TestClass(), 3), -2);
    assertIntEquals(testLoop25(new TestClass(), 0), 2);
    assertIntEquals(testLoop25(new TestClass(), 1), 2);
    assertIntEquals(testLoop25(new TestClass(), 2), 4);
    assertIntEquals(testLoop25(new TestClass(), 3), -1);
    assertIntEquals(testLoop26(new TestClass(), 0), 1);
    assertIntEquals(testLoop26(new TestClass(), 1), 0);
    assertIntEquals(testLoop26(new TestClass(), 2), 0);
    assertIntEquals(testLoop26(new TestClass(), 3), 0);
    assertIntEquals(testLoop27(new TestClass(), 0), 1);
    assertIntEquals(testLoop27(new TestClass(), 1), 1);
    assertIntEquals(testLoop27(new TestClass(), 2), 0);
    assertIntEquals(testLoop27(new TestClass(), 3), 0);
    assertIntEquals(testLoop28(new TestClass(1, 2), 0), 0);
    assertIntEquals(testLoop28(new TestClass(1, 2), 1), 0);
    assertIntEquals(testLoop28(new TestClass(1, 2), 2), 0);
    assertIntEquals(testLoop28(new TestClass(1, 2), 3), 1);
    assertIntEquals(testLoop29(0), 0);
    assertIntEquals(testLoop29(1), 1);
    assertIntEquals(testLoop29(2), 3);
    assertIntEquals(testLoop29(3), 6);
    assertIntEquals(testLoop30(0), 0);
    assertIntEquals(testLoop30(1), 1);
    assertIntEquals(testLoop30(2), 2);
    assertIntEquals(testLoop30(3), 3);
    assertIntEquals(testLoop31(0), 0);
    assertIntEquals(testLoop31(1), 0);
    assertIntEquals(testLoop31(2), 0);
    assertIntEquals(testLoop31(3), 0);
    assertIntEquals(testLoop32(new TestClass(), 0), -1);
    assertIntEquals(testLoop32(new TestClass(), 1), 2);
    assertIntEquals(testLoop32(new TestClass(), 2), 1);
    assertIntEquals(testLoop32(new TestClass(), 3), -2);
    assertIntEquals(testLoop33(new TestClass(), 0), 0);
    assertIntEquals(testLoop33(new TestClass(), 1), 0);
    assertIntEquals(testLoop33(new TestClass(), 2), 0);
    assertIntEquals(testLoop33(new TestClass(), 3), 0);
    assertIntEquals(testLoop34(0), 0);
    assertIntEquals(testLoop34(1), 1);
    assertIntEquals(testLoop34(2), 3);
    assertIntEquals(testLoop34(3), 6);
    assertIntEquals(testLoop35(0), 0);
    assertIntEquals(testLoop35(1), 1);
    assertIntEquals(testLoop35(2), 3);
    assertIntEquals(testLoop35(3), 6);
    assertIntEquals(testLoop36(4), 6);
    assertIntEquals(testLoop37(4), 6);
    assertIntEquals(testLoop38(4, new int[4]), 0);

    assertIntEquals(testNestedLoop1(new TestClass(), 0), 1);
    assertIntEquals(testNestedLoop1(new TestClass(), 1), 1);
    assertIntEquals(testNestedLoop1(new TestClass(), 2), 1);
    assertIntEquals(testNestedLoop1(new TestClass(), 3), 1);
    assertIntEquals(testNestedLoop2(new TestClass(), 0), 1);
    assertIntEquals(testNestedLoop2(new TestClass(), 1), 2);
    assertIntEquals(testNestedLoop2(new TestClass(), 2), 2);
    assertIntEquals(testNestedLoop2(new TestClass(), 3), 2);
    assertIntEquals(testNestedLoop3(new TestClass(), 0), 1);
    assertIntEquals(testNestedLoop3(new TestClass(), 1), 2);
    assertIntEquals(testNestedLoop3(new TestClass(), 2), 2);
    assertIntEquals(testNestedLoop3(new TestClass(), 3), 2);
    assertIntEquals(testNestedLoop4(new TestClass(), 0), 1);
    assertIntEquals(testNestedLoop4(new TestClass(), 1), 1);
    assertIntEquals(testNestedLoop4(new TestClass(), 2), 2);
    assertIntEquals(testNestedLoop4(new TestClass(), 3), 2);
    assertIntEquals(testNestedLoop5(new TestClass(), 0), 1);
    assertIntEquals(testNestedLoop5(new TestClass(), 1), 2);
    assertIntEquals(testNestedLoop5(new TestClass(), 2), 2);
    assertIntEquals(testNestedLoop5(new TestClass(), 3), 2);
    assertIntEquals(testNestedLoop6(new TestClass(), 0), 1);
    assertIntEquals(testNestedLoop6(new TestClass(), 1), 1);
    assertIntEquals(testNestedLoop6(new TestClass(), 2), 2);
    assertIntEquals(testNestedLoop6(new TestClass(), 3), 2);
    assertIntEquals(testNestedLoop7(new TestClass(), 0), 0);
    assertIntEquals(testNestedLoop7(new TestClass(), 1), 1);
    assertIntEquals(testNestedLoop7(new TestClass(), 2), 2);
    assertIntEquals(testNestedLoop7(new TestClass(), 3), 3);
    assertIntEquals(testNestedLoop8(new TestClass(), 0), 0);
    assertIntEquals(testNestedLoop8(new TestClass(), 1), 0);
    assertIntEquals(testNestedLoop8(new TestClass(), 2), 0);
    assertIntEquals(testNestedLoop8(new TestClass(), 3), 0);
    assertLongEquals(testOverlapLoop(10), 34l);
    assertLongEquals(testOverlapLoop(50), 7778742049l);
    assertIntEquals($noinline$testPartialEscape1(new TestClass(), true), 1);
    assertIntEquals($noinline$testPartialEscape1(new TestClass(), false), 0);
    assertIntEquals($noinline$testPartialEscape2(new TestClass(), true), 1);
    assertIntEquals($noinline$testPartialEscape2(new TestClass(), false), 0);
    assertDoubleEquals($noinline$testPartialEscape3_double(true), -20d);
    assertDoubleEquals($noinline$testPartialEscape3_double(false), -40d);
    assertFloatEquals($noinline$testPartialEscape3_float(true), -20f);
    assertFloatEquals($noinline$testPartialEscape3_float(false), -40f);
    assertIntEquals($noinline$testPartialEscape3_int(true), -20);
    assertIntEquals($noinline$testPartialEscape3_int(false), -40);
    assertIntEquals($noinline$testPartialEscape3_byte(true), -20);
    assertIntEquals($noinline$testPartialEscape3_byte(false), -40);
    assertIntEquals($noinline$testPartialEscape3_short(true), -20);
    assertIntEquals($noinline$testPartialEscape3_short(false), -40);
    assertLongEquals($noinline$testPartialEscape3_long(true), -20);
    assertLongEquals($noinline$testPartialEscape3_long(false), -40);
  }
}
