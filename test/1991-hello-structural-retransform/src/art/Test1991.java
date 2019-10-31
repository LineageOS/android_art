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
public class Test1991 {

  static class Transform {
    public static void sayHi() {
      System.out.println("hello");
    }
  }


  /**
   * base64 encoded class/dex file for
   * static class Transform {
   *   public static void sayHi() {
   *    System.out.println("I say hello and " + sayGoodbye());
   *   }
   *   public static String sayGoodbye() {
   *     return "you say goodbye!";
   *   }
   * }
   */
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQCi0OGZvVpTRbHGfNbo3bfcu60kPpJayMgoBQAAcAAAAHhWNBIAAAAAAAAAAGQEAAAc" +
    "AAAAcAAAAAoAAADgAAAABAAAAAgBAAABAAAAOAEAAAgAAABAAQAAAQAAAIABAACIAwAAoAEAAC4C" +
    "AAA2AgAASAIAAEsCAABPAgAAaQIAAHkCAACdAgAAvQIAANQCAADoAgAA/AIAABcDAAArAwAAOgMA" +
    "AEUDAABIAwAATAMAAFkDAABhAwAAZwMAAGwDAAB1AwAAgQMAAIgDAACSAwAAmQMAAKsDAAAEAAAA" +
    "BQAAAAYAAAAHAAAACAAAAAkAAAAKAAAACwAAAAwAAAAPAAAAAgAAAAYAAAAAAAAAAwAAAAcAAAAo" +
    "AgAADwAAAAkAAAAAAAAAEAAAAAkAAAAoAgAACAAEABQAAAAAAAIAAAAAAAAAAAAWAAAAAAACABcA" +
    "AAAEAAMAFQAAAAUAAgAAAAAABwACAAAAAAAHAAEAEgAAAAcAAAAYAAAAAAAAAAAAAAAFAAAAAAAA" +
    "AA0AAABUBAAAMgQAAAAAAAABAAAAAAAAABoCAAADAAAAGgAaABEAAAABAAEAAQAAABYCAAAEAAAA" +
    "cBAEAAAADgAEAAAAAgAAAB4CAAAbAAAAYgAAAHEAAQAAAAwBIgIHAHAQBQACABoDAQBuIAYAMgBu" +
    "IAYAEgBuEAcAAgAMAW4gAwAQAA4ABgAOAAsADgAIAA4BGg8AAAAAAQAAAAYABjxpbml0PgAQSSBz" +
    "YXkgaGVsbG8gYW5kIAABTAACTEwAGExhcnQvVGVzdDE5OTEkVHJhbnNmb3JtOwAOTGFydC9UZXN0" +
    "MTk5MTsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3Rh" +
    "dGlvbi9Jbm5lckNsYXNzOwAVTGphdmEvaW8vUHJpbnRTdHJlYW07ABJMamF2YS9sYW5nL09iamVj" +
    "dDsAEkxqYXZhL2xhbmcvU3RyaW5nOwAZTGphdmEvbGFuZy9TdHJpbmdCdWlsZGVyOwASTGphdmEv" +
    "bGFuZy9TeXN0ZW07AA1UZXN0MTk5MS5qYXZhAAlUcmFuc2Zvcm0AAVYAAlZMAAthY2Nlc3NGbGFn" +
    "cwAGYXBwZW5kAARuYW1lAANvdXQAB3ByaW50bG4ACnNheUdvb2RieWUABXNheUhpAAh0b1N0cmlu" +
    "ZwAFdmFsdWUAEHlvdSBzYXkgZ29vZGJ5ZSEAdn5+RDh7ImNvbXBpbGF0aW9uLW1vZGUiOiJkZWJ1" +
    "ZyIsIm1pbi1hcGkiOjEsInNoYS0xIjoiNjBkYTRkNjdiMzgxYzQyNDY3NzU3YzQ5ZmI2ZTU1NzU2" +
    "ZDg4YTJmMyIsInZlcnNpb24iOiIxLjcuMTItZGV2In0AAgIBGRgBAgMCEQQIExcOAAADAACAgAS4" +
    "AwEJoAMBCdADAAAAAAIAAAAjBAAAKQQAAEgEAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAEAAAAAAAAA" +
    "AQAAABwAAABwAAAAAgAAAAoAAADgAAAAAwAAAAQAAAAIAQAABAAAAAEAAAA4AQAABQAAAAgAAABA" +
    "AQAABgAAAAEAAACAAQAAASAAAAMAAACgAQAAAyAAAAMAAAAWAgAAARAAAAEAAAAoAgAAAiAAABwA" +
    "AAAuAgAABCAAAAIAAAAjBAAAACAAAAEAAAAyBAAAAxAAAAIAAABEBAAABiAAAAEAAABUBAAAABAA" +
    "AAEAAABkBAAA");


  public static void run() {
    Redefinition.setTestConfiguration(Redefinition.Config.STRUCTURAL_TRANSFORM);
    doTest();
  }

  public static void doTest() {
    Transform.sayHi();
    Redefinition.addCommonTransformationResult("art/Test1991$Transform", new byte[0], DEX_BYTES);
    Redefinition.enableCommonRetransformation(true);
    Redefinition.doCommonClassRetransformation(Transform.class);
    Transform.sayHi();
  }
}
