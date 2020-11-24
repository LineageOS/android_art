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

/**
 * Check that the classes using publicly listed APIS are verified.
 */

class AccessPublicCtor {
  public Integer foo() {
    return new Integer(1);
  }
}

class AccessPublicMethod {
  public double foo(Integer i) {
    return i.doubleValue();
  }
}

class AccessPublicMethodFromParent {
  public void foo(Integer i) {
    i.notify();
  }
}

class AccessPublicStaticMethod {
  public Integer foo() {
    return Integer.getInteger("1");
  }
}

class AccessPublicStaticField {
  public int foo() {
    return Integer.BYTES;
  }
}

/**
 * Check that the classes using non publicly listed APIS are not verified.
 */

class AccessNonPublicCtor {
  public Integer foo() {
    return new Integer("1");
  }
}

class AccessNonPublicMethod {
  public float foo(Integer i) {
    return i.floatValue();
  }
}

class AccessNonPublicMethodFromParent {
  public void foo(Integer i) {
    i.notifyAll();
  }
}

class AccessNonPublicStaticMethod {
  public Integer foo() {
    return Integer.getInteger("1", 0);
  }
}

class AccessNonPublicStaticField {
  public Class foo() {
    return Integer.TYPE;
  }
}
