/*
 * Copyright (C) 2018 The Android Open Source Project
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

import java.lang.reflect.InvocationTargetException;

public class Main {
    public static void main(String[] args) throws Throwable {
        // Class BadField is defined in BadField.smali.
        Class<?> c = Class.forName("BadField");

        // Storing null is OK.
        c.getMethod("storeStaticNull").invoke(null);
        c.getMethod("storeInstanceNull").invoke(null);

        testStoreObject(c, "storeStaticObject");
        testStoreObject(c, "storeInstanceObject");
    }

    public static void testStoreObject(Class<?> c, String methodName) throws Throwable{
        try {
          // Storing anything else should throw an exception.
          c.getMethod(methodName).invoke(null);
          throw new Error("Expected NoClassDefFoundError");
        } catch (InvocationTargetException expected) {
          Throwable e = expected.getCause();
          if (e instanceof NoClassDefFoundError) {
            // The NoClassDefFoundError is for the field widget in class BadField.
            if (!e.getMessage().equals("Failed resolution of: LWidget;")) {
                throw new Error("Unexpected " + e);
            }
          } else {
            throw new Error("Unexpected " + e);
          }
        }
    }
}
