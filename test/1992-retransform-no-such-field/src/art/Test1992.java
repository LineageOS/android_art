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

import java.util.Base64;
public class Test1992 {
  public static boolean FAIL_IT = false;

  static class Transform {
    public void saySomething() {
      System.out.println("This file was written in the year 2019!");
    }
  }

  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public void saySomething() {
   *    if (Test1992.FAIL_IT) {
   *      // Force verification soft-fail.
   *      Test1992.NOT_THERE = 1;
   *    }
   *    System.out.println("This new class was written in " + year);
   *   }
   * }
   */
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
"yv66vgAAADUAKQoACAARCQASABMJABIAFAkAFQAWCAAXCgAYABkHABoHAB0BAAY8aW5pdD4BAAMo" +
"KVYBAARDb2RlAQAPTGluZU51bWJlclRhYmxlAQAMc2F5U29tZXRoaW5nAQANU3RhY2tNYXBUYWJs" +
"ZQEAClNvdXJjZUZpbGUBAA1UZXN0MTk5Mi5qYXZhDAAJAAoHAB4MAB8AIAwAIQAiBwAjDAAkACUB" +
"ACJUaGlzIG5ldyBjbGFzcyB3YXMgd3JpdHRlbiBpbiAyMDE5BwAmDAAnACgBABZhcnQvVGVzdDE5" +
"OTIkVHJhbnNmb3JtAQAJVHJhbnNmb3JtAQAMSW5uZXJDbGFzc2VzAQAQamF2YS9sYW5nL09iamVj" +
"dAEADGFydC9UZXN0MTk5MgEAB0ZBSUxfSVQBAAFaAQAJTk9UX1RIRVJFAQABSQEAEGphdmEvbGFu" +
"Zy9TeXN0ZW0BAANvdXQBABVMamF2YS9pby9QcmludFN0cmVhbTsBABNqYXZhL2lvL1ByaW50U3Ry" +
"ZWFtAQAHcHJpbnRsbgEAFShMamF2YS9sYW5nL1N0cmluZzspVgAgAAcACAAAAAAAAgAAAAkACgAB" +
"AAsAAAAdAAEAAQAAAAUqtwABsQAAAAEADAAAAAYAAQAAAAUAAQANAAoAAQALAAAAQAACAAEAAAAT" +
"sgACmQAHBLMAA7IABBIFtgAGsQAAAAIADAAAABIABAAAAAkABgAKAAoADAASAA0ADgAAAAMAAQoA" +
"AgAPAAAAAgAQABwAAAAKAAEABwASABsACA==");

  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
"ZGV4CjAzNQAWyivK2j0yR4u2YH/R8Bs3KIFv7O/Hs0WkBAAAcAAAAHhWNBIAAAAAAAAAAOADAAAZ" +
"AAAAcAAAAAsAAADUAAAAAgAAAAABAAADAAAAGAEAAAQAAAAwAQAAAQAAAFABAAA0AwAAcAEAAMoB" +
"AADSAQAA2wEAAN4BAAD4AQAACAIAACwCAABMAgAAYwIAAHcCAACLAgAAnwIAAKoCAAC5AgAA3QIA" +
"AOgCAADrAgAA7wIAAPICAAD/AgAABQMAAAoDAAATAwAAIQMAACgDAAACAAAAAwAAAAQAAAAFAAAA" +
"BgAAAAcAAAAIAAAACQAAAAoAAAAPAAAAEQAAAA8AAAAJAAAAAAAAABAAAAAJAAAAxAEAAAIACgAB" +
"AAAAAgAAAAsAAAAIAAUAFAAAAAEAAAAAAAAAAQAAABYAAAAFAAEAFQAAAAYAAAAAAAAAAQAAAAAA" +
"AAAGAAAAAAAAAAwAAADQAwAArwMAAAAAAAABAAEAAQAAALYBAAAEAAAAcBADAAAADgADAAEAAgAA" +
"ALoBAAAPAAAAYwAAADgABQASEGcAAQBiAAIAGgENAG4gAgAQAA4ABQAOAAkADks9eAAAAAABAAAA" +
"BwAGPGluaXQ+AAdGQUlMX0lUAAFJABhMYXJ0L1Rlc3QxOTkyJFRyYW5zZm9ybTsADkxhcnQvVGVz" +
"dDE5OTI7ACJMZGFsdmlrL2Fubm90YXRpb24vRW5jbG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90" +
"YXRpb24vSW5uZXJDbGFzczsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGphdmEvbGFuZy9PYmpl" +
"Y3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAEkxqYXZhL2xhbmcvU3lzdGVtOwAJTk9UX1RIRVJFAA1U" +
"ZXN0MTk5Mi5qYXZhACJUaGlzIG5ldyBjbGFzcyB3YXMgd3JpdHRlbiBpbiAyMDE5AAlUcmFuc2Zv" +
"cm0AAVYAAlZMAAFaAAthY2Nlc3NGbGFncwAEbmFtZQADb3V0AAdwcmludGxuAAxzYXlTb21ldGhp" +
"bmcABXZhbHVlAHZ+fkQ4eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJtaW4tYXBpIjoxLCJz" +
"aGEtMSI6IjYwZGE0ZDY3YjM4MWM0MjQ2Nzc1N2M0OWZiNmU1NTc1NmQ4OGEyZjMiLCJ2ZXJzaW9u" +
"IjoiMS43LjEyLWRldiJ9AAIDARcYAgIEAhIECBMXDgAAAQEAgIAE8AIBAYgDAAAAAAAAAAIAAACg" +
"AwAApgMAAMQDAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAEAAAAAAAAAAQAAABkAAABwAAAAAgAAAAsA" +
"AADUAAAAAwAAAAIAAAAAAQAABAAAAAMAAAAYAQAABQAAAAQAAAAwAQAABgAAAAEAAABQAQAAASAA" +
"AAIAAABwAQAAAyAAAAIAAAC2AQAAARAAAAEAAADEAQAAAiAAABkAAADKAQAABCAAAAIAAACgAwAA" +
"ACAAAAEAAACvAwAAAxAAAAIAAADAAwAABiAAAAEAAADQAwAAABAAAAEAAADgAwAA");



  public static void run() {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest(new Transform());
  }

  public static void doTest(Transform t) {
    t.saySomething();
    Redefinition.doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
    t.saySomething();
  }
}
