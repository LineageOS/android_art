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

public class Transform1976 {

  static {
  }

  /* Dex file for:
   * package art;
   * public class Transform1976 {
   *   static {}
   *   public static byte[] REDEFINED_DEX_BYTES;
   *   public static void sayEverything() {
   *     sayHi();
   *     sayBye();
   *   }
   *   public static void sayBye() {
   *     System.out.println("Bye");
   *   }
   *   public static void sayHi() {
   *     System.out.println("Not saying hi again!");
   *   }
   * }
   */
  public static byte[] REDEFINED_DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQBHoxOnl1VNY5YvAENBMpZs9rgNOtJjgZFEBAAAcAAAAHhWNBIAAAAAAAAAAJgDAAAU"
                  + "AAAAcAAAAAcAAADAAAAAAgAAANwAAAACAAAA9AAAAAcAAAAEAQAAAQAAADwBAADoAgAAXAEAAAYC"
                  + "AAAQAgAAGAIAAB0CAAAyAgAASQIAAF0CAABxAgAAhQIAAJsCAACwAgAAxAIAAMcCAADLAgAAzwIA"
                  + "ANQCAADdAgAA5QIAAPQCAAD7AgAAAwAAAAQAAAAFAAAABgAAAAcAAAALAAAADQAAAAsAAAAFAAAA"
                  + "AAAAAAwAAAAFAAAAAAIAAAAABgAJAAAABAABAA4AAAAAAAAAAAAAAAAAAAABAAAAAAAAABAAAAAA"
                  + "AAAAEQAAAAAAAAASAAAAAQABAA8AAAACAAAAAQAAAAAAAAABAAAAAgAAAAAAAAAKAAAAAAAAAHMD"
                  + "AAAAAAAAAAAAAAAAAADoAQAAAQAAAA4AAAABAAEAAQAAAOwBAAAEAAAAcBAGAAAADgACAAAAAgAA"
                  + "APABAAAIAAAAYgABABoBAgBuIAUAEAAOAAAAAAAAAAAA9QEAAAcAAABxAAQAAABxAAIAAAAOAAAA"
                  + "AgAAAAIAAAD7AQAACAAAAGIAAQAaAQgAbiAFABAADgADAA4AAgAOAAoADngABgAOPDwADQAOeAAB"
                  + "AAAAAwAIPGNsaW5pdD4ABjxpbml0PgADQnllABNMYXJ0L1RyYW5zZm9ybTE5NzY7ABVMamF2YS9p"
                  + "by9QcmludFN0cmVhbTsAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGphdmEvbGFuZy9TdHJpbmc7ABJM"
                  + "amF2YS9sYW5nL1N5c3RlbTsAFE5vdCBzYXlpbmcgaGkgYWdhaW4hABNSRURFRklORURfREVYX0JZ"
                  + "VEVTABJUcmFuc2Zvcm0xOTc2LmphdmEAAVYAAlZMAAJbQgADb3V0AAdwcmludGxuAAZzYXlCeWUA"
                  + "DXNheUV2ZXJ5dGhpbmcABXNheUhpAHZ+fkQ4eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJt"
                  + "aW4tYXBpIjoxLCJzaGEtMSI6ImE4MzUyZjI1NDg4NTM2MmNjZDhkOTA5ZDM1MjljNjAwOTRkZDg5"
                  + "NmUiLCJ2ZXJzaW9uIjoiMS42LjIwLWRldiJ9AAEABQAACQCIgATcAgGBgATwAgEJiAMBCagDAQnI"
                  + "AwAAAAAAAAAOAAAAAAAAAAEAAAAAAAAAAQAAABQAAABwAAAAAgAAAAcAAADAAAAAAwAAAAIAAADc"
                  + "AAAABAAAAAIAAAD0AAAABQAAAAcAAAAEAQAABgAAAAEAAAA8AQAAASAAAAUAAABcAQAAAyAAAAUA"
                  + "AADoAQAAARAAAAEAAAAAAgAAAiAAABQAAAAGAgAAACAAAAEAAABzAwAAAxAAAAEAAACUAwAAABAA"
                  + "AAEAAACYAwAA");

  public static void sayEverything() {
    sayHi();
  }

  public static void sayHi() {
    System.out.println("hello");
  }
}
