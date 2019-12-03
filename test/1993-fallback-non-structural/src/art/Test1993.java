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

package art;

import java.util.Base64;
import java.lang.reflect.*;
public class Test1993 {

  static class Transform {
    public void sayHi() {
      // Use lower 'h' to make sure the string will have a different string id
      // than the transformation (the transformation code is the same except
      // the actual printed String, which was making the test inacurately passing
      // in JIT mode when loading the string from the dex cache, as the string ids
      // of the two different strings were the same).
      // We know the string ids will be different because lexicographically:
      // "Goodbye" < "LTransform;" < "hello".
      System.out.println("hello");
    }
  }

  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public void sayHi() {
   *    System.out.println("Goodbye");
   *   }
   * }
   */
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQDxrdbiBcsn0r58mdtcdyDxVUxwbWfShNQwBAAAcAAAAHhWNBIAAAAAAAAAAGwDAAAV" +
    "AAAAcAAAAAkAAADEAAAAAgAAAOgAAAABAAAAAAEAAAQAAAAIAQAAAQAAACgBAADoAgAASAEAAJIB" +
    "AACaAQAAowEAAL0BAADNAQAA8QEAABECAAAoAgAAPAIAAFACAABkAgAAcwIAAH4CAACBAgAAhQIA" +
    "AJICAACYAgAAnQIAAKYCAACtAgAAtAIAAAIAAAADAAAABAAAAAUAAAAGAAAABwAAAAgAAAAJAAAA" +
    "DAAAAAwAAAAIAAAAAAAAAA0AAAAIAAAAjAEAAAcABAAQAAAAAAAAAAAAAAAAAAAAEgAAAAQAAQAR" +
    "AAAABQAAAAAAAAAAAAAAAAAAAAUAAAAAAAAACgAAAFwDAAA7AwAAAAAAAAEAAQABAAAAgAEAAAQA" +
    "AABwEAMAAAAOAAMAAQACAAAAhAEAAAgAAABiAAAAGgEBAG4gAgAQAA4AAwAOAAUADngAAAAAAQAA" +
    "AAYABjxpbml0PgAHR29vZGJ5ZQAYTGFydC9UZXN0MTk5MyRUcmFuc2Zvcm07AA5MYXJ0L1Rlc3Qx" +
    "OTkzOwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xvc2luZ0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0" +
    "aW9uL0lubmVyQ2xhc3M7ABVMamF2YS9pby9QcmludFN0cmVhbTsAEkxqYXZhL2xhbmcvT2JqZWN0" +
    "OwASTGphdmEvbGFuZy9TdHJpbmc7ABJMamF2YS9sYW5nL1N5c3RlbTsADVRlc3QxOTkzLmphdmEA" +
    "CVRyYW5zZm9ybQABVgACVkwAC2FjY2Vzc0ZsYWdzAARuYW1lAANvdXQAB3ByaW50bG4ABXNheUhp" +
    "AAV2YWx1ZQB2fn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwibWluLWFwaSI6MSwic2hh" +
    "LTEiOiJjZDkwMDIzOTMwZDk3M2Y1NzcxMWYxZDRmZGFhZDdhM2U0NzE0NjM3IiwidmVyc2lvbiI6" +
    "IjEuNy4xNC1kZXYifQACAgETGAECAwIOBAgPFwsAAAEBAICABMgCAQHgAgAAAAAAAAACAAAALAMA" +
    "ADIDAABQAwAAAAAAAAAAAAAAAAAAEAAAAAAAAAABAAAAAAAAAAEAAAAVAAAAcAAAAAIAAAAJAAAA" +
    "xAAAAAMAAAACAAAA6AAAAAQAAAABAAAAAAEAAAUAAAAEAAAACAEAAAYAAAABAAAAKAEAAAEgAAAC" +
    "AAAASAEAAAMgAAACAAAAgAEAAAEQAAABAAAAjAEAAAIgAAAVAAAAkgEAAAQgAAACAAAALAMAAAAg" +
    "AAABAAAAOwMAAAMQAAACAAAATAMAAAYgAAABAAAAXAMAAAAQAAABAAAAbAMAAA==");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest(new Transform());
  }

  public static void doTest(Transform t) throws Exception {
    System.out.println("Can structurally Redefine: " +
      Redefinition.isStructurallyModifiable(Transform.class));
    t.sayHi();
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    t.sayHi();
    // Check and make sure we didn't structurally redefine by looking for ClassExt.obsoleteClass
    Field ext_data_field = Class.class.getDeclaredField("extData");
    ext_data_field.setAccessible(true);
    Object ext_data = ext_data_field.get(Transform.class);
    Field obsolete_class_field = ext_data.getClass().getDeclaredField("obsoleteClass");
    obsolete_class_field.setAccessible(true);
    if (obsolete_class_field.get(ext_data) != null)  {
      System.out.println("Expected no ClassExt.obsoleteClass but got " + obsolete_class_field.get(ext_data));
    }
  }
}
