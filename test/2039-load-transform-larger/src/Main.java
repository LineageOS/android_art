/*
 * Copyright (C) 2021 The Android Open Source Project
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

import static art.Redefinition.addCommonTransformationResult;
import static art.Redefinition.enableCommonRetransformation;
import static art.Redefinition.setPopRetransformations;

import java.lang.reflect.*;
import java.util.Base64;

class Main {
  public static String TEST_NAME = "2039-load-transform-larger";

  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public static int PAD1;
   *   public static int PAD2;
   *   public static int PAD3;
   *   public static int PAD4;
   *   public static int PAD5;
   *   public static int PAD6;
   *   public static int PAD7;
   *   public static int PAD8;
   *   public static int PAD9;
   *   public static int PAD10;
   *   public static String TO_SAY = "Goodbye";
   *   public void sayHi() {
   *     System.out.println(TO_SAY);
   *   }
   * }
   */
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADcAQAoAEQAnCQAoACkJABAAKgoAKwAsCQAQAC0JABAALgkAEAAvCQAQADAJABAAMQkA" +
    "EAAyCQAQADMJABAANAkAEAA1CQAQADYIADcHADgHADkBAARQQUQxAQABSQEABFBBRDIBAARQQUQz" +
    "AQAEUEFENAEABFBBRDUBAARQQUQ2AQAEUEFENwEABFBBRDgBAARQQUQ5AQAFUEFEMTABAAZUT19T" +
    "QVkBABJMamF2YS9sYW5nL1N0cmluZzsBAAY8aW5pdD4BAAMoKVYBAARDb2RlAQAPTGluZU51bWJl" +
    "clRhYmxlAQAFc2F5SGkBAAg8Y2xpbml0PgEAClNvdXJjZUZpbGUBAA5UcmFuc2Zvcm0uamF2YQwA" +
    "HwAgBwA6DAA7ADwMAB0AHgcAPQwAPgA/DAASABMMABQAEwwAFQATDAAWABMMABcAEwwAGAATDAAZ" +
    "ABMMABoAEwwAGwATDAAcABMBAAdHb29kYnllAQAJVHJhbnNmb3JtAQAQamF2YS9sYW5nL09iamVj" +
    "dAEAEGphdmEvbGFuZy9TeXN0ZW0BAANvdXQBABVMamF2YS9pby9QcmludFN0cmVhbTsBABNqYXZh" +
    "L2lvL1ByaW50U3RyZWFtAQAHcHJpbnRsbgEAFShMamF2YS9sYW5nL1N0cmluZzspVgAgABAAEQAA" +
    "AAsACQASABMAAAAJABQAEwAAAAkAFQATAAAACQAWABMAAAAJABcAEwAAAAkAGAATAAAACQAZABMA" +
    "AAAJABoAEwAAAAkAGwATAAAACQAcABMAAAAJAB0AHgAAAAMAAAAfACAAAQAhAAAAHQABAAEAAAAF" +
    "KrcAAbEAAAABACIAAAAGAAEAAAABAAEAIwAgAAEAIQAAACYAAgABAAAACrIAArIAA7YABLEAAAAB" +
    "ACIAAAAKAAIAAAAOAAkADwAIACQAIAABACEAAABuAAEAAAAAAC4DswAFA7MABgOzAAcDswAIA7MA" +
    "CQOzAAoDswALA7MADAOzAA0DswAOEg+zAAOxAAAAAQAiAAAALgALAAAAAgAEAAMACAAEAAwABQAQ" +
    "AAYAFAAHABgACAAcAAkAIAAKACQACwAoAAwAAQAlAAAAAgAm");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQDwqDsREETVXjkzn+/MRpRRrCJUZTv/xY28BAAAcAAAAHhWNBIAAAAAAAAAABAEAAAb" +
    "AAAAcAAAAAcAAADcAAAAAgAAAPgAAAAMAAAAEAEAAAUAAABwAQAAAQAAAJgBAAAEAwAAuAEAAFIC" +
    "AABcAgAAZAIAAG0CAABwAgAAfQIAAJQCAACoAgAAvAIAANACAADWAgAA3QIAAOMCAADpAgAA7wIA" +
    "APUCAAD7AgAAAQMAAAcDAAANAwAAFQMAACUDAAAoAwAALAMAADEDAAA6AwAAQQMAAAMAAAAEAAAA" +
    "BQAAAAYAAAAHAAAACAAAABUAAAAVAAAABgAAAAAAAAAWAAAABgAAAEwCAAABAAAACQAAAAEAAAAK" +
    "AAAAAQAAAAsAAAABAAAADAAAAAEAAAANAAAAAQAAAA4AAAABAAAADwAAAAEAAAAQAAAAAQAAABEA" +
    "AAABAAAAEgAAAAEABAATAAAABQACABcAAAABAAAAAAAAAAEAAAABAAAAAQAAABkAAAACAAEAGAAA" +
    "AAMAAAABAAAAAQAAAAAAAAADAAAAAAAAABQAAAAAAAAA4AMAAAAAAAABAAAAAAAAADQCAAAaAAAA" +
    "EgBnAAAAZwACAGcAAwBnAAQAZwAFAGcABgBnAAcAZwAIAGcACQBnAAEAGgACAGkACgAOAAEAAQAB" +
    "AAAAQgIAAAQAAABwEAQAAAAOAAMAAQACAAAARgIAAAgAAABiAAsAYgEKAG4gAwAQAA4AAgAOPC0t" +
    "LS0tLS0tLQABAA4ADgAOeAAAAQAAAAQACDxjbGluaXQ+AAY8aW5pdD4AB0dvb2RieWUAAUkAC0xU" +
    "cmFuc2Zvcm07ABVMamF2YS9pby9QcmludFN0cmVhbTsAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGph" +
    "dmEvbGFuZy9TdHJpbmc7ABJMamF2YS9sYW5nL1N5c3RlbTsABFBBRDEABVBBRDEwAARQQUQyAARQ" +
    "QUQzAARQQUQ0AARQQUQ1AARQQUQ2AARQQUQ3AARQQUQ4AARQQUQ5AAZUT19TQVkADlRyYW5zZm9y" +
    "bS5qYXZhAAFWAAJWTAADb3V0AAdwcmludGxuAAVzYXlIaQCcAX5+RDh7ImJhY2tlbmQiOiJkZXgi" +
    "LCJjb21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJoYXMtY2hlY2tzdW1zIjpmYWxzZSwibWluLWFw" +
    "aSI6MSwic2hhLTEiOiIzZmU0MDM4NDA1NTUyNTU3YzFjNjNhZTIxNjM5OGUzMzFiNWViZThkIiwi" +
    "dmVyc2lvbiI6IjMuMC4zMC1kZXYifQALAAIBAAkBCQEJAQkBCQEJAQkBCQEJAQkBCQCIgAS4AwGA" +
    "gAT8AwIBlAQAAAAAAAAOAAAAAAAAAAEAAAAAAAAAAQAAABsAAABwAAAAAgAAAAcAAADcAAAAAwAA" +
    "AAIAAAD4AAAABAAAAAwAAAAQAQAABQAAAAUAAABwAQAABgAAAAEAAACYAQAAASAAAAMAAAC4AQAA" +
    "AyAAAAMAAAA0AgAAARAAAAEAAABMAgAAAiAAABsAAABSAgAAACAAAAEAAADgAwAAAxAAAAEAAAAM" +
    "BAAAABAAAAEAAAAQBAAA");

  public static ClassLoader getClassLoaderFor(String location) throws Exception {
    try {
      Class<?> class_loader_class = Class.forName("dalvik.system.PathClassLoader");
      Constructor<?> ctor = class_loader_class.getConstructor(String.class, ClassLoader.class);
      /* on Dalvik, this is a DexFile; otherwise, it's null */
      return (ClassLoader)ctor.newInstance(location + "/" + TEST_NAME + "-ex.jar",
                                           Main.class.getClassLoader());
    } catch (ClassNotFoundException e) {
      // Running on RI. Use URLClassLoader.
      return new java.net.URLClassLoader(
          new java.net.URL[] { new java.net.URL("file://" + location + "/classes-ex/") });
    }
  }

  public static void main(String[] args) {
    // Don't pop transformations. Make sure that even if 2 threads race to define the class both
    // will get the same result.
    setPopRetransformations(false);
    addCommonTransformationResult("Transform", CLASS_BYTES, DEX_BYTES);
    enableCommonRetransformation(true);
    try {
      /* this is the "alternate" DEX/Jar file */
      ClassLoader new_loader = getClassLoaderFor(System.getenv("DEX_LOCATION"));
      Class<?> klass = (Class<?>)new_loader.loadClass("TestMain");
      if (klass == null) {
        throw new AssertionError("loadClass failed");
      }
      Method run_test = klass.getMethod("runTest");
      run_test.invoke(null);
    } catch (Exception e) {
      System.out.println(e.toString());
      e.printStackTrace(System.out);
    }
  }
}
