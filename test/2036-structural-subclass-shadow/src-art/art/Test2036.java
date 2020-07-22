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

import java.util.Base64;

public class Test2036 {
  public static class Transform {
    public Transform() {}
  }

  public static class SubTransform extends Transform {
    public SubTransform() {}

    public static int FooBar() {
      return 1337;
    }

    public int foo = 42;
    public static double STATIC_FOO = 3.14d;
  }

  public static class SubSubTransform extends SubTransform {
    public SubSubTransform() {}
  }

  public static class SubSubSubTransform extends SubSubTransform {
    public SubSubSubTransform() {}

    public int getFoo() {
      return this.foo;
    }
  }
  /*
   * base64 encoded class/dex file for
   * Base64 generated using:
   * % javac Test2036.java
   * % d8 Test2035\$Transform.class
   * % base64 classes.dex| sed 's:^:":' | sed 's:$:" +:'
   *
   * package art;
   * public static class Transform {
   *   public Transform() {}
   *   public int bar;
   * }
   */
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQAZlSz5hewOnpRnplr0AnEqh8+AKNc2qhp0AwAAcAAAAHhWNBIAAAAAAAAAALwCAAAP"
                  + "AAAAcAAAAAcAAACsAAAAAQAAAMgAAAABAAAA1AAAAAIAAADcAAAAAQAAAOwAAABoAgAADAEAACgB"
                  + "AAAwAQAAMwEAAE0BAABdAQAAgQEAAKEBAAC1AQAAxAEAAM8BAADSAQAA3wEAAOQBAADqAQAA8QEA"
                  + "AAEAAAACAAAAAwAAAAQAAAAFAAAABgAAAAkAAAAJAAAABgAAAAAAAAABAAAACwAAAAEAAAAAAAAA"
                  + "BQAAAAAAAAABAAAAAQAAAAUAAAAAAAAABwAAAKwCAACOAgAAAAAAAAEAAQABAAAAJAEAAAQAAABw"
                  + "EAEAAAAOABgADgAGPGluaXQ+AAFJABhMYXJ0L1Rlc3QyMDM2JFRyYW5zZm9ybTsADkxhcnQvVGVz"
                  + "dDIwMzY7ACJMZGFsdmlrL2Fubm90YXRpb24vRW5jbG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90"
                  + "YXRpb24vSW5uZXJDbGFzczsAEkxqYXZhL2xhbmcvT2JqZWN0OwANVGVzdDIwMzYuamF2YQAJVHJh"
                  + "bnNmb3JtAAFWAAthY2Nlc3NGbGFncwADYmFyAARuYW1lAAV2YWx1ZQCLAX5+RDh7ImNvbXBpbGF0"
                  + "aW9uLW1vZGUiOiJkZWJ1ZyIsImhhcy1jaGVja3N1bXMiOmZhbHNlLCJtaW4tYXBpIjoxLCJzaGEt"
                  + "MSI6IjJkMDE0MjEyNjVlMjUxMWM5MGUxZTY2NTU0NWEzMzliM2M5OWMwYWYiLCJ2ZXJzaW9uIjoi"
                  + "Mi4yLjMtZGV2In0AAgMBDRgCAgQCCgQJDBcIAAEBAAABAIGABIwCAAAAAAAAAgAAAH8CAACFAgAA"
                  + "oAIAAAAAAAAAAAAAAAAAAA8AAAAAAAAAAQAAAAAAAAABAAAADwAAAHAAAAACAAAABwAAAKwAAAAD"
                  + "AAAAAQAAAMgAAAAEAAAAAQAAANQAAAAFAAAAAgAAANwAAAAGAAAAAQAAAOwAAAABIAAAAQAAAAwB"
                  + "AAADIAAAAQAAACQBAAACIAAADwAAACgBAAAEIAAAAgAAAH8CAAAAIAAAAQAAAI4CAAADEAAAAgAA"
                  + "AJwCAAAGIAAAAQAAAKwCAAAAEAAAAQAAALwCAAA=");
  /*
   * base64 encoded class/dex file for
   * Base64 generated using:
   * % javac Test2036.java
   * % d8 Test2035\$SubSubTransform.class
   * % base64 classes.dex| sed 's:^:":' | sed 's:$:" +:'
   *
   * package art;
   * public static class SubSubTransform extends SubTransform {
   *   public SubTransform() {}
   *   public static int FooBar() { return 0xDEADBEEF; }
   *   public int foo;
   *   public static double STATIC_FOO;
   * }
   */
  private static final byte[] DEX_BYTES_SUB_SUB =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQATpeOoP5d8wSzfcaS99KwXbb1DsEne1tDsAwAAcAAAAHhWNBIAAAAAAAAAADQDAAAS"
                  + "AAAAcAAAAAgAAAC4AAAAAgAAANgAAAACAAAA8AAAAAMAAAAAAQAAAQAAABgBAAC0AgAAOAEAAHAB"
                  + "AAB4AQAAewEAAIMBAACGAQAApgEAAMMBAADTAQAA9wEAABcCAAAjAgAANAIAAEMCAABGAgAAUwIA"
                  + "AFgCAABeAgAAZQIAAAEAAAADAAAABAAAAAUAAAAGAAAABwAAAAgAAAAMAAAAAwAAAAEAAAAAAAAA"
                  + "DAAAAAcAAAAAAAAAAgAAAAkAAAACAAEADgAAAAIAAQAAAAAAAgAAAAIAAAADAAEAAAAAAAIAAAAB"
                  + "AAAAAwAAAAAAAAALAAAAJAMAAAIDAAAAAAAAAQAAAAAAAABoAQAABAAAABQA776t3g8AAQABAAEA"
                  + "AABsAQAABAAAAHAQAgAAAA4AIQAOACAADgAGPGluaXQ+AAFEAAZGb29CYXIAAUkAHkxhcnQvVGVz"
                  + "dDIwMzYkU3ViU3ViVHJhbnNmb3JtOwAbTGFydC9UZXN0MjAzNiRTdWJUcmFuc2Zvcm07AA5MYXJ0"
                  + "L1Rlc3QyMDM2OwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xvc2luZ0NsYXNzOwAeTGRhbHZpay9h"
                  + "bm5vdGF0aW9uL0lubmVyQ2xhc3M7AApTVEFUSUNfRk9PAA9TdWJTdWJUcmFuc2Zvcm0ADVRlc3Qy"
                  + "MDM2LmphdmEAAVYAC2FjY2Vzc0ZsYWdzAANmb28ABG5hbWUABXZhbHVlAIsBfn5EOHsiY29tcGls"
                  + "YXRpb24tbW9kZSI6ImRlYnVnIiwiaGFzLWNoZWNrc3VtcyI6ZmFsc2UsIm1pbi1hcGkiOjEsInNo"
                  + "YS0xIjoiMmQwMTQyMTI2NWUyNTExYzkwZTFlNjY1NTQ1YTMzOWIzYzk5YzBhZiIsInZlcnNpb24i"
                  + "OiIyLjIuMy1kZXYifQACBQEQGAQCBgINBAkPFwoBAQIAAAkBAQCBgATQAgEJuAIAAAAAAgAAAPMC"
                  + "AAD5AgAAGAMAAAAAAAAAAAAAAAAAAA8AAAAAAAAAAQAAAAAAAAABAAAAEgAAAHAAAAACAAAACAAA"
                  + "ALgAAAADAAAAAgAAANgAAAAEAAAAAgAAAPAAAAAFAAAAAwAAAAABAAAGAAAAAQAAABgBAAABIAAA"
                  + "AgAAADgBAAADIAAAAgAAAGgBAAACIAAAEgAAAHABAAAEIAAAAgAAAPMCAAAAIAAAAQAAAAIDAAAD"
                  + "EAAAAgAAABQDAAAGIAAAAQAAACQDAAAAEAAAAQAAADQDAAA=");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static void doTest() throws Exception {
    SubSubSubTransform t = new SubSubSubTransform();
    System.out.println("value-getter is " + t.getFoo());
    System.out.println("value is " + t.foo);
    System.out.println("static-call is " + SubSubSubTransform.FooBar());
    System.out.println("static-value is " + SubSubSubTransform.STATIC_FOO);
    Redefinition.doMultiStructuralClassRedefinition(
        new Redefinition.DexOnlyClassDefinition(Transform.class, DEX_BYTES),
        new Redefinition.DexOnlyClassDefinition(SubSubTransform.class, DEX_BYTES_SUB_SUB));
    System.out.println("value-getter is " + t.getFoo());
    System.out.println("value is " + t.foo);
    System.out.println("static-call is " + SubSubSubTransform.FooBar());
    System.out.println("static-value is " + SubSubSubTransform.STATIC_FOO);
  }
}
