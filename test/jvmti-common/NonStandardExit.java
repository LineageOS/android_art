/*
 * Copyright (C) 2019 The Android Open Source Project
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

public class NonStandardExit {
  public static native void popFrame(Thread thr);
  public static native void forceEarlyReturnVoid(Thread thr);
  public static native void forceEarlyReturnFloat(Thread thr, float f);
  public static native void forceEarlyReturnDouble(Thread thr, double f);
  public static native void forceEarlyReturnInt(Thread thr, int f);
  public static native void forceEarlyReturnLong(Thread thr, long f);
  public static native void forceEarlyReturnObject(Thread thr, Object f);

  public static void forceEarlyReturn(Thread thr, Object o) {
    if (o instanceof Number && o.getClass().getPackage().equals(Object.class.getPackage())) {
      Number n = (Number)o;
      if (n instanceof Integer || n instanceof Short || n instanceof Byte) {
        forceEarlyReturnInt(thr, n.intValue());
      } else if (n instanceof Long) {
        forceEarlyReturnLong(thr, n.longValue());
      } else if (n instanceof Float) {
        forceEarlyReturnFloat(thr, n.floatValue());
      } else if (n instanceof Double) {
        forceEarlyReturnDouble(thr, n.doubleValue());
      } else {
        throw new IllegalArgumentException("Unknown number subtype: " + n.getClass() + " - " + n);
      }
    } else if (o instanceof Character) {
      forceEarlyReturnInt(thr, ((Character)o).charValue());
    } else if (o instanceof Boolean) {
      forceEarlyReturnInt(thr, ((Boolean)o).booleanValue() ? 1 : 0);
    } else {
      forceEarlyReturnObject(thr, o);
    }
  }
}
