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

import java.lang.reflect.Field;
import java.util.Base64;

import sun.misc.Unsafe;

public class Test1977 {

  // The class we will be transforming.
  static class Transform {
    static {
    }

    public static String sayHiName = " Alex";
    // Called whenever we do something.
    public static void somethingHappened() {}

    public static void sayHi(Runnable r) {
      System.out.println("hello" + sayHiName);
      r.run();
      somethingHappened();
      System.out.println("goodbye" + sayHiName);
    }
  }

  // static class Transform {
  //   static {}
  //   // NB Due to the ordering of fields offset of sayHiName will change.
  //   public static String sayHiName;
  //   public static String sayByeName;
  //   public static void somethingHappened() {
  //     sayByeName = " and good luck";
  //   }
  //   public static void doSayBye() {
  //     System.out.println("Goodbye" + sayByeName + " - Transformed");
  //   }
  //   public static void doSayHi() {
  //     System.out.println("Hello" + sayHiName + " - Transformed");
  //   }
  //   public static void sayHi(Runnable r) {
  //     doSayHi();
  //     r.run();
  //     somethingHappened();
  //     doSayBye();
  //   }
  // }
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQBNCReVL85UCydGe4wKq3olUYP6Lb8WIlewBgAAcAAAAHhWNBIAAAAAAAAAAOwFAAAl"
                  + "AAAAcAAAAAsAAAAEAQAABQAAADABAAADAAAAbAEAAAwAAACEAQAAAQAAAOQBAACsBAAABAIAAEID"
                  + "AABSAwAAYgMAAGwDAAB0AwAAfQMAAIQDAACHAwAAiwMAAKUDAAC1AwAA2QMAAPkDAAAQBAAAJAQA"
                  + "ADoEAABOBAAAaQQAAH0EAACMBAAAlwQAAJoEAACeBAAAqwQAALMEAAC9BAAAxgQAAMwEAADRBAAA"
                  + "2gQAAN8EAADrBAAA8gQAAP0EAAAQBQAAGgUAACEFAAAIAAAACQAAAAoAAAALAAAADAAAAA0AAAAO"
                  + "AAAADwAAABAAAAARAAAAFAAAAAYAAAAHAAAAAAAAAAcAAAAIAAAANAMAABQAAAAKAAAAAAAAABUA"
                  + "AAAKAAAAPAMAABUAAAAKAAAANAMAAAAABwAeAAAAAAAHACAAAAAJAAQAGwAAAAAAAgACAAAAAAAC"
                  + "AAMAAAAAAAIAGAAAAAAAAgAZAAAAAAADAB8AAAAAAAIAIQAAAAQABAAcAAAABQACAAMAAAAGAAIA"
                  + "HQAAAAgAAgADAAAACAABABcAAAAIAAAAIgAAAAAAAAAAAAAABQAAAAAAAAASAAAA3AUAAKgFAAAA"
                  + "AAAAAAAAAAAAAAAOAwAAAQAAAA4AAAABAAEAAQAAABIDAAAEAAAAcBAHAAAADgAEAAAAAgAAABYD"
                  + "AAAeAAAAYgACAGIBAAAiAggAcBAJAAIAGgMEAG4gCgAyAG4gCgASABoBAABuIAoAEgBuEAsAAgAM"
                  + "AW4gBgAQAA4ABAAAAAIAAAAdAwAAHgAAAGIAAgBiAQEAIgIIAHAQCQACABoDBQBuIAoAMgBuIAoA"
                  + "EgAaAQAAbiAKABIAbhALAAIADAFuIAYAEAAOAAEAAQABAAAAJAMAAA0AAABxAAMAAAByEAgAAABx"
                  + "AAUAAABxAAIAAAAOAAAAAQAAAAAAAAAtAwAABQAAABoAAQBpAAAADgAHAA4ABgAOAA8ADgEdDwAS"
                  + "AA4BHQ8AFQEADjw8PDwADAAOSwAAAAEAAAAHAAAAAQAAAAYADiAtIFRyYW5zZm9ybWVkAA4gYW5k"
                  + "IGdvb2QgbHVjawAIPGNsaW5pdD4ABjxpbml0PgAHR29vZGJ5ZQAFSGVsbG8AAUwAAkxMABhMYXJ0"
                  + "L1Rlc3QxOTc3JFRyYW5zZm9ybTsADkxhcnQvVGVzdDE5Nzc7ACJMZGFsdmlrL2Fubm90YXRpb24v"
                  + "RW5jbG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90YXRpb24vSW5uZXJDbGFzczsAFUxqYXZhL2lv"
                  + "L1ByaW50U3RyZWFtOwASTGphdmEvbGFuZy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwAS"
                  + "TGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7ABJMamF2YS9sYW5n"
                  + "L1N5c3RlbTsADVRlc3QxOTc3LmphdmEACVRyYW5zZm9ybQABVgACVkwAC2FjY2Vzc0ZsYWdzAAZh"
                  + "cHBlbmQACGRvU2F5QnllAAdkb1NheUhpAARuYW1lAANvdXQAB3ByaW50bG4AA3J1bgAKc2F5Qnll"
                  + "TmFtZQAFc2F5SGkACXNheUhpTmFtZQARc29tZXRoaW5nSGFwcGVuZWQACHRvU3RyaW5nAAV2YWx1"
                  + "ZQB2fn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwibWluLWFwaSI6MSwic2hhLTEiOiJh"
                  + "ODM1MmYyNTQ4ODUzNjJjY2Q4ZDkwOWQzNTI5YzYwMDk0ZGQ4OTZlIiwidmVyc2lvbiI6IjEuNi4y"
                  + "MC1kZXYifQACAgEjGAECAwIWBAgaFxMCAAYAAAkBCQCIgASEBAGAgASYBAEJsAQBCfwEAQnIBQEJ"
                  + "9AUAAAAAAgAAAJkFAACfBQAA0AUAAAAAAAAAAAAAAAAAABAAAAAAAAAAAQAAAAAAAAABAAAAJQAA"
                  + "AHAAAAACAAAACwAAAAQBAAADAAAABQAAADABAAAEAAAAAwAAAGwBAAAFAAAADAAAAIQBAAAGAAAA"
                  + "AQAAAOQBAAABIAAABgAAAAQCAAADIAAABgAAAA4DAAABEAAAAgAAADQDAAACIAAAJQAAAEIDAAAE"
                  + "IAAAAgAAAJkFAAAAIAAAAQAAAKgFAAADEAAAAgAAAMwFAAAGIAAAAQAAANwFAAAAEAAAAQAAAOwF"
                  + "AAA=");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static void doTest() throws Exception {
    Transform.sayHi(
        () -> {
          System.out.println("Not doing anything here");
        });
    Transform.sayHi(
        () -> {
          System.out.println("transforming calling function");
          Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
        });
    Transform.sayHi(
        () -> {
          System.out.println("Not doing anything here");
        });
  }
}
