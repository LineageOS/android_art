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
public class Test1994 {

  public static final class Transform {
    public void sayHi() {
      System.out.println("Hi!");
    }
  }

  /**
   * base64 encoded class/dex file for
   * public static final class Transform {
   *   public void sayHi() {
   *     sayHiEnglish();
   *     sayHiDanish();
   *     sayHiFrance();
   *     sayHiJapan();
   *   }
   *   public void sayHiEnglish() {
   *     System.out.println("Hello world!");
   *   }
   *   public void sayHiDanish() {
   *     System.out.println("Hej Verden!");
   *   }
   *   public void sayHiJapan() {
   *     System.out.println("こんにちは世界!");
   *   }
   *   public void sayHiFrance() {
   *     System.out.println("Bonjour le monde!");
   *   }
   * }
   */
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQA87tn3VIDgMrF+Md2W4r58elaMPcSfk2CMBQAAcAAAAHhWNBIAAAAAAAAAAMgEAAAc" +
    "AAAAcAAAAAkAAADgAAAAAgAAAAQBAAABAAAAHAEAAAgAAAAkAQAAAQAAAGQBAAAIBAAAhAEAAG4C" +
    "AAB2AgAAiQIAAJYCAACkAgAAvgIAAM4CAADyAgAAEgMAACkDAAA9AwAAUQMAAGUDAAB0AwAAfwMA" +
    "AIIDAACGAwAAkwMAAJkDAACeAwAApwMAAK4DAAC7AwAAyQMAANYDAADiAwAA6QMAAGEEAAAEAAAA" +
    "BQAAAAYAAAAHAAAACAAAAAkAAAAKAAAACwAAAA4AAAAOAAAACAAAAAAAAAAPAAAACAAAAGgCAAAH" +
    "AAQAEgAAAAAAAAAAAAAAAAAAABQAAAAAAAAAFQAAAAAAAAAWAAAAAAAAABcAAAAAAAAAGAAAAAQA" +
    "AQATAAAABQAAAAAAAAAAAAAAEQAAAAUAAAAAAAAADAAAALgEAACIBAAAAAAAAAEAAQABAAAASAIA" +
    "AAQAAABwEAcAAAAOAAEAAQABAAAATAIAAA0AAABuEAMAAABuEAIAAABuEAQAAABuEAUAAAAOAAAA" +
    "AwABAAIAAABUAgAACAAAAGIAAAAaAQIAbiAGABAADgADAAEAAgAAAFkCAAAIAAAAYgAAABoBAwBu" +
    "IAYAEAAOAAMAAQACAAAAXgIAAAgAAABiAAAAGgEBAG4gBgAQAA4AAwABAAIAAABjAgAACAAAAGIA" +
    "AAAaARsAbiAGABAADgADAA4ABQAOPDw8PAAOAA54AAsADngAFAAOeAARAA54AAEAAAAGAAY8aW5p" +
    "dD4AEUJvbmpvdXIgbGUgbW9uZGUhAAtIZWogVmVyZGVuIQAMSGVsbG8gd29ybGQhABhMYXJ0L1Rl" +
    "c3QxOTk0JFRyYW5zZm9ybTsADkxhcnQvVGVzdDE5OTQ7ACJMZGFsdmlrL2Fubm90YXRpb24vRW5j" +
    "bG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90YXRpb24vSW5uZXJDbGFzczsAFUxqYXZhL2lvL1By" +
    "aW50U3RyZWFtOwASTGphdmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAEkxqYXZh" +
    "L2xhbmcvU3lzdGVtOwANVGVzdDE5OTQuamF2YQAJVHJhbnNmb3JtAAFWAAJWTAALYWNjZXNzRmxh" +
    "Z3MABG5hbWUAA291dAAHcHJpbnRsbgAFc2F5SGkAC3NheUhpRGFuaXNoAAxzYXlIaUVuZ2xpc2gA" +
    "C3NheUhpRnJhbmNlAApzYXlIaUphcGFuAAV2YWx1ZQB2fn5EOHsiY29tcGlsYXRpb24tbW9kZSI6" +
    "ImRlYnVnIiwibWluLWFwaSI6MSwic2hhLTEiOiJjZDkwMDIzOTMwZDk3M2Y1NzcxMWYxZDRmZGFh" +
    "ZDdhM2U0NzE0NjM3IiwidmVyc2lvbiI6IjEuNy4xNC1kZXYifQAI44GT44KT44Gr44Gh44Gv5LiW" +
    "55WMIQACAgEZGAECAwIQBBkRFw0AAAEFAIGABIQDAQGcAwEByAMBAegDAQGIBAEBqAQAAAAAAAAC" +
    "AAAAeQQAAH8EAACsBAAAAAAAAAAAAAAAAAAAEAAAAAAAAAABAAAAAAAAAAEAAAAcAAAAcAAAAAIA" +
    "AAAJAAAA4AAAAAMAAAACAAAABAEAAAQAAAABAAAAHAEAAAUAAAAIAAAAJAEAAAYAAAABAAAAZAEA" +
    "AAEgAAAGAAAAhAEAAAMgAAAGAAAASAIAAAEQAAABAAAAaAIAAAIgAAAcAAAAbgIAAAQgAAACAAAA" +
    "eQQAAAAgAAABAAAAiAQAAAMQAAACAAAAqAQAAAYgAAABAAAAuAQAAAAQAAABAAAAyAQAAA==");

  public static void run() {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest(new Transform());
  }

  public static void doTest(Transform t) {
    t.sayHi();
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    t.sayHi();
  }
}
