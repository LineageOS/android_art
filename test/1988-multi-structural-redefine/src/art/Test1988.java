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

public class Test1988 {
  static class Transform1 {
    public static void sayHi() {
      System.out.println("hello - Transform 1");
    }
  }
  static class Transform2 {
    public static void sayHi() {
      System.out.println("hello - Transform 2");
    }
  }

  /** Base64 encoded dex file for
   *
   * static class Trasnform1 {
   *   public static void sayHi() {
   *     System.out.println("Transform1 says hi and " + Transform2.getBye());
   *   }
   *   public static String getBye() {
   *     return "Transform1 says bye!";
   *   }
   * }
   */
  public static final byte[] T1_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQAU4pPI4BKgrMtz7s1Ogc8in1PQhazaRWBcBQAAcAAAAHhWNBIAAAAAAAAAAJgEAAAd" +
    "AAAAcAAAAAsAAADkAAAABAAAABABAAABAAAAQAEAAAkAAABIAQAAAQAAAJABAACsAwAAsAEAAD4C" +
    "AABGAgAASQIAAE0CAABoAgAAgwIAAJMCAAC3AgAA1wIAAO4CAAACAwAAFgMAADEDAABFAwAAVAMA" +
    "AGADAAB2AwAAjwMAAJIDAACWAwAAowMAAKsDAACzAwAAuQMAAL4DAADHAwAAzgMAANgDAADfAwAA" +
    "AwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACQAAAAoAAAALAAAADAAAABEAAAABAAAABwAAAAAAAAAC" +
    "AAAACAAAADgCAAARAAAACgAAAAAAAAASAAAACgAAADgCAAAJAAUAFwAAAAAAAgAAAAAAAAAAABUA" +
    "AAAAAAIAGQAAAAEAAAAVAAAABQADABgAAAAGAAIAAAAAAAgAAgAAAAAACAABABQAAAAIAAAAGgAA" +
    "AAAAAAAAAAAABgAAAAAAAAANAAAAiAQAAGUEAAAAAAAAAQAAAAAAAAAqAgAAAwAAABoADwARAAAA" +
    "AQABAAEAAAAmAgAABAAAAHAQBQAAAA4ABAAAAAIAAAAuAgAAGwAAAGIAAABxAAMAAAAMASICCABw" +
    "EAYAAgAaAxAAbiAHADIAbiAHABIAbhAIAAIADAFuIAQAEAAOAAYADgALAA4ACAAOARoPAAAAAAEA" +
    "AAAHAAY8aW5pdD4AAUwAAkxMABlMYXJ0L1Rlc3QxOTg4JFRyYW5zZm9ybTE7ABlMYXJ0L1Rlc3Qx" +
    "OTg4JFRyYW5zZm9ybTI7AA5MYXJ0L1Rlc3QxOTg4OwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xv" +
    "c2luZ0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xhc3M7ABVMamF2YS9pby9Qcmlu" +
    "dFN0cmVhbTsAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9s" +
    "YW5nL1N0cmluZ0J1aWxkZXI7ABJMamF2YS9sYW5nL1N5c3RlbTsADVRlc3QxOTg4LmphdmEAClRy" +
    "YW5zZm9ybTEAFFRyYW5zZm9ybTEgc2F5cyBieWUhABdUcmFuc2Zvcm0xIHNheXMgaGkgYW5kIAAB" +
    "VgACVkwAC2FjY2Vzc0ZsYWdzAAZhcHBlbmQABmdldEJ5ZQAEbmFtZQADb3V0AAdwcmludGxuAAVz" +
    "YXlIaQAIdG9TdHJpbmcABXZhbHVlAHV+fkQ4eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJt" +
    "aW4tYXBpIjoxLCJzaGEtMSI6IjY4NjQ4NTU3NTM0MDJiYmFjODk2Nzc2YjAzN2RlYmJjOTM4YzQ5" +
    "NTMiLCJ2ZXJzaW9uIjoiMS43LjYtZGV2In0AAgMBGxgCAgQCEwQIFhcOAAADAACAgATIAwEJsAMB" +
    "CeADAAAAAAACAAAAVgQAAFwEAAB8BAAAAAAAAAAAAAAAAAAAEAAAAAAAAAABAAAAAAAAAAEAAAAd" +
    "AAAAcAAAAAIAAAALAAAA5AAAAAMAAAAEAAAAEAEAAAQAAAABAAAAQAEAAAUAAAAJAAAASAEAAAYA" +
    "AAABAAAAkAEAAAEgAAADAAAAsAEAAAMgAAADAAAAJgIAAAEQAAABAAAAOAIAAAIgAAAdAAAAPgIA" +
    "AAQgAAACAAAAVgQAAAAgAAABAAAAZQQAAAMQAAACAAAAeAQAAAYgAAABAAAAiAQAAAAQAAABAAAA" +
    "mAQAAA==");


  /** Base64 encoded dex file for
   *
   * static class Trasnform2 {
   *   public static void sayHi() {
   *     System.out.println("Transform2 says hi and " + Transform1.getBye());
   *   }
   *   public static String getBye() {
   *     return "Transform2 says bye!";
   *   }
   * }
   */
  public static final byte[] T2_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQD94cwR+R7Yw7VMom5CwuQd5mZlsV2xrVFcBQAAcAAAAHhWNBIAAAAAAAAAAJgEAAAd" +
    "AAAAcAAAAAsAAADkAAAABAAAABABAAABAAAAQAEAAAkAAABIAQAAAQAAAJABAACsAwAAsAEAAD4C" +
    "AABGAgAASQIAAE0CAABoAgAAgwIAAJMCAAC3AgAA1wIAAO4CAAACAwAAFgMAADEDAABFAwAAVAMA" +
    "AGADAAB2AwAAjwMAAJIDAACWAwAAowMAAKsDAACzAwAAuQMAAL4DAADHAwAAzgMAANgDAADfAwAA" +
    "AwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACQAAAAoAAAALAAAADAAAABEAAAABAAAABwAAAAAAAAAC" +
    "AAAACAAAADgCAAARAAAACgAAAAAAAAASAAAACgAAADgCAAAJAAUAFwAAAAAAAAAVAAAAAQACAAAA" +
    "AAABAAAAFQAAAAEAAgAZAAAABQADABgAAAAGAAIAAAAAAAgAAgAAAAAACAABABQAAAAIAAAAGgAA" +
    "AAEAAAAAAAAABgAAAAAAAAANAAAAiAQAAGUEAAAAAAAAAQAAAAAAAAAqAgAAAwAAABoADwARAAAA" +
    "AQABAAEAAAAmAgAABAAAAHAQBQAAAA4ABAAAAAIAAAAuAgAAGwAAAGIAAABxAAAAAAAMASICCABw" +
    "EAYAAgAaAxAAbiAHADIAbiAHABIAbhAIAAIADAFuIAQAEAAOAA4ADgATAA4AEAAOARoPAAAAAAEA" +
    "AAAHAAY8aW5pdD4AAUwAAkxMABlMYXJ0L1Rlc3QxOTg4JFRyYW5zZm9ybTE7ABlMYXJ0L1Rlc3Qx" +
    "OTg4JFRyYW5zZm9ybTI7AA5MYXJ0L1Rlc3QxOTg4OwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xv" +
    "c2luZ0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xhc3M7ABVMamF2YS9pby9Qcmlu" +
    "dFN0cmVhbTsAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9s" +
    "YW5nL1N0cmluZ0J1aWxkZXI7ABJMamF2YS9sYW5nL1N5c3RlbTsADVRlc3QxOTg4LmphdmEAClRy" +
    "YW5zZm9ybTIAFFRyYW5zZm9ybTIgc2F5cyBieWUhABdUcmFuc2Zvcm0yIHNheXMgaGkgYW5kIAAB" +
    "VgACVkwAC2FjY2Vzc0ZsYWdzAAZhcHBlbmQABmdldEJ5ZQAEbmFtZQADb3V0AAdwcmludGxuAAVz" +
    "YXlIaQAIdG9TdHJpbmcABXZhbHVlAHV+fkQ4eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJt" +
    "aW4tYXBpIjoxLCJzaGEtMSI6IjY4NjQ4NTU3NTM0MDJiYmFjODk2Nzc2YjAzN2RlYmJjOTM4YzQ5" +
    "NTMiLCJ2ZXJzaW9uIjoiMS43LjYtZGV2In0AAgMBGxgCAgQCEwQIFhcOAAADAAGAgATIAwEJsAMB" +
    "CeADAAAAAAACAAAAVgQAAFwEAAB8BAAAAAAAAAAAAAAAAAAAEAAAAAAAAAABAAAAAAAAAAEAAAAd" +
    "AAAAcAAAAAIAAAALAAAA5AAAAAMAAAAEAAAAEAEAAAQAAAABAAAAQAEAAAUAAAAJAAAASAEAAAYA" +
    "AAABAAAAkAEAAAEgAAADAAAAsAEAAAMgAAADAAAAJgIAAAEQAAABAAAAOAIAAAIgAAAdAAAAPgIA" +
    "AAQgAAACAAAAVgQAAAAgAAABAAAAZQQAAAMQAAACAAAAeAQAAAYgAAABAAAAiAQAAAAQAAABAAAA" +
    "mAQAAA==");


  public static void run() {
    doTest();
  }

  public static void doTest() {
    Transform1.sayHi();
    Transform2.sayHi();
    System.out.println(
        "Redefining both " + Transform1.class + " and " + Transform2.class + " to use each other.");
    Redefinition.doMultiStructuralClassRedefinition(
        new Redefinition.DexOnlyClassDefinition(Transform1.class, T1_BYTES),
        new Redefinition.DexOnlyClassDefinition(Transform2.class, T2_BYTES));
    Transform1.sayHi();
    Transform2.sayHi();
  }
}
