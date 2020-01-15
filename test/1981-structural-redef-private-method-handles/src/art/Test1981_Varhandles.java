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

package art;

import java.lang.invoke.*;
import java.lang.ref.*;
import java.lang.reflect.*;
import java.util.*;

public class Test1981_Varhandles implements Test1981.VarHandler {
  public Test1981_Varhandles() {}

  public boolean doVarHandleTests() {
    return true;
  }

  public Object findStaticVarHandle(MethodHandles.Lookup l, Class c, String n, Class t)
      throws Throwable {
    return l.findStaticVarHandle(c, n, t);
  }

  public Object get(Object vh) throws Throwable {
    return ((VarHandle) vh).get();
  }

  public void set(Object vh, Object v) throws Throwable {
    ((VarHandle) vh).set(v);
  }
  public boolean instanceofVarHandle(Object v) {
    return v instanceof VarHandle;
  }
  public Object getVarTypeName(Object v) {
    return ((VarHandle)v).varType().getName();
  }
}
