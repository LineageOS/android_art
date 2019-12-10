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
public class Test2003 {

  public static class Transform {
    public String getGreeting() {
      return "Hi";
    }
  }

  public static class SubTransform extends Transform {
    private int count = 0;
    public void sayHi() {
      System.out.println(getGreeting() + "(SubTransform called " + (++count) + " times)");
    }
  }
  /**
   * base64 encoded class/dex file for
   * public static class SubTransform extends Transform {
   *   private int count = 0;
   *   public void sayHi() {
   *     System.out.println(getGreeting() + "(SubTransform called " + (++count) + " times, Transform called " + getCount() + " times)");
   *   }
   * }
   */
  private static final byte[] SUBTRANSFORM_DEX_BYTES = Base64.getDecoder().decode(
"ZGV4CjAzNQCh/FHVi1J2bdQclTBNUHHeAbrR2Sy2cWXoBQAAcAAAAHhWNBIAAAAAAAAAACQFAAAh" +
"AAAAcAAAAAsAAAD0AAAABgAAACABAAACAAAAaAEAAAoAAAB4AQAAAQAAAMgBAAAABAAA6AEAAJ4C" +
"AACnAgAAwgIAANkCAADhAgAA5AIAAOcCAADrAgAA7wIAAAwDAAAmAwAANgMAAFoDAAB6AwAAkQMA" +
"AKUDAADAAwAA1AMAAOIDAADxAwAA9AMAAPgDAAAFBAAADQQAABQEAAAeBAAAKwQAADEEAAA2BAAA" +
"PwQAAEYEAABQBAAAVwQAAAQAAAAIAAAACQAAAAoAAAALAAAADAAAAA0AAAAOAAAADwAAABAAAAAT" +
"AAAABAAAAAAAAAAAAAAABQAAAAcAAAAAAAAABgAAAAgAAACQAgAABwAAAAgAAACYAgAAEwAAAAoA" +
"AAAAAAAAFAAAAAoAAACYAgAAAQAAABcAAAAJAAYAGwAAAAEABAADAAAAAQAAABgAAAABAAEAGQAA" +
"AAEABAAdAAAAAgAEAAMAAAAGAAUAHAAAAAgABAADAAAACAACABYAAAAIAAMAFgAAAAgAAQAeAAAA" +
"AQAAAAEAAAACAAAAAAAAABIAAAAUBQAA9AQAAAAAAAACAAEAAQAAAIICAAAHAAAAcBAEAAEAEgBZ" +
"EAAADgAAAAYAAQACAAAAhwIAADUAAABiAAEAbhACAAUADAFSUgAA2AICAVlSAABuEAEABQAKAyIE" +
"CABwEAYABABuIAgAFAAaAQIAbiAIABQAbiAHACQAGgEBAG4gCAAUAG4gBwA0ABoBAABuIAgAFABu" +
"EAkABAAMAW4gBQAQAA4ABQAOPAAIAA4BNA8AAAABAAAAAAAAAAEAAAAHAAcgdGltZXMpABkgdGlt" +
"ZXMsIFRyYW5zZm9ybSBjYWxsZWQgABUoU3ViVHJhbnNmb3JtIGNhbGxlZCAABjxpbml0PgABSQAB" +
"TAACTEkAAkxMABtMYXJ0L1Rlc3QyMDAzJFN1YlRyYW5zZm9ybTsAGExhcnQvVGVzdDIwMDMkVHJh" +
"bnNmb3JtOwAOTGFydC9UZXN0MjAwMzsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFz" +
"czsAHkxkYWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwAVTGphdmEvaW8vUHJpbnRTdHJlYW07" +
"ABJMamF2YS9sYW5nL1N0cmluZzsAGUxqYXZhL2xhbmcvU3RyaW5nQnVpbGRlcjsAEkxqYXZhL2xh" +
"bmcvU3lzdGVtOwAMU3ViVHJhbnNmb3JtAA1UZXN0MjAwMy5qYXZhAAFWAAJWTAALYWNjZXNzRmxh" +
"Z3MABmFwcGVuZAAFY291bnQACGdldENvdW50AAtnZXRHcmVldGluZwAEbmFtZQADb3V0AAdwcmlu" +
"dGxuAAVzYXlIaQAIdG9TdHJpbmcABXZhbHVlAIsBfn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRl" +
"YnVnIiwiaGFzLWNoZWNrc3VtcyI6ZmFsc2UsIm1pbi1hcGkiOjEsInNoYS0xIjoiODViZjE2Yzc1" +
"NjUzZDQwNGE0YzNlZDQzNjA3Yzc3Yjg1YmFmMzFlZSIsInZlcnNpb24iOiIyLjAuNS1kZXYifQAC" +
"BAEfGAMCBQIVBAkaFxEAAQEBAAIAgYAE6AMDAYgEAAAAAAIAAADlBAAA6wQAAAgFAAAAAAAAAAAA" +
"AAAAAAAQAAAAAAAAAAEAAAAAAAAAAQAAACEAAABwAAAAAgAAAAsAAAD0AAAAAwAAAAYAAAAgAQAA" +
"BAAAAAIAAABoAQAABQAAAAoAAAB4AQAABgAAAAEAAADIAQAAASAAAAIAAADoAQAAAyAAAAIAAACC" +
"AgAAARAAAAIAAACQAgAAAiAAACEAAACeAgAABCAAAAIAAADlBAAAACAAAAEAAAD0BAAAAxAAAAIA" +
"AAAEBQAABiAAAAEAAAAUBQAAABAAAAEAAAAkBQAA");

  /**
   * base64 encoded class/dex file for
   * public static class Transform {
   *   private int count;
   *   public String getGreeting() {
   *     incrCount();
   *     return "Hello";
   *   }
   *   protected void incrCount() {
   *     ++count;
   *   }
   *   protected int getCount() {
   *     return count;
   *   }
   * }
   */
  private static final byte[] TRANSFORM_DEX_BYTES = Base64.getDecoder().decode(
"ZGV4CjAzNQCAt16FlKvFzDaE6l56jUkorc7YXyrJmRpsBAAAcAAAAHhWNBIAAAAAAAAAALQDAAAV" +
"AAAAcAAAAAgAAADEAAAAAwAAAOQAAAABAAAACAEAAAUAAAAQAQAAAQAAADgBAAAUAwAAWAEAANQB" +
"AADcAQAA4wEAAOYBAADpAQAAAwIAABMCAAA3AgAAVwIAAGsCAAB/AgAAjgIAAJkCAACcAgAAqQIA" +
"ALACAAC6AgAAxwIAANICAADYAgAA3wIAAAIAAAAEAAAABQAAAAYAAAAHAAAACAAAAAkAAAAMAAAA" +
"AgAAAAAAAAAAAAAAAwAAAAYAAAAAAAAADAAAAAcAAAAAAAAAAQAAAA4AAAABAAIAAAAAAAEAAAAP" +
"AAAAAQABABAAAAABAAIAEQAAAAUAAgAAAAAAAQAAAAEAAAAFAAAAAAAAAAoAAACkAwAAfAMAAAAA" +
"AAACAAEAAAAAAMYBAAADAAAAUhAAAA8AAAACAAEAAQAAAMoBAAAGAAAAbhADAAEAGgABABEAAQAB" +
"AAEAAADCAQAABAAAAHAQBAAAAA4AAgABAAAAAADPAQAABwAAAFIQAADYAAABWRAAAA4ACwAOABUA" +
"DgAOAA48ABIADmkABjxpbml0PgAFSGVsbG8AAUkAAUwAGExhcnQvVGVzdDIwMDMkVHJhbnNmb3Jt" +
"OwAOTGFydC9UZXN0MjAwMzsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxk" +
"YWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwASTGphdmEvbGFuZy9PYmplY3Q7ABJMamF2YS9s" +
"YW5nL1N0cmluZzsADVRlc3QyMDAzLmphdmEACVRyYW5zZm9ybQABVgALYWNjZXNzRmxhZ3MABWNv" +
"dW50AAhnZXRDb3VudAALZ2V0R3JlZXRpbmcACWluY3JDb3VudAAEbmFtZQAFdmFsdWUAiwF+fkQ4" +
"eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJoYXMtY2hlY2tzdW1zIjpmYWxzZSwibWluLWFw" +
"aSI6MSwic2hhLTEiOiI4NWJmMTZjNzU2NTNkNDA0YTRjM2VkNDM2MDdjNzdiODViYWYzMWVlIiwi" +
"dmVyc2lvbiI6IjIuMC41LWRldiJ9AAIDARMYAgIEAg0ECRIXCwABAQMAAgCBgASMAwEE2AIBAfAC" +
"AQSkAwAAAAACAAAAbQMAAHMDAACYAwAAAAAAAAAAAAAAAAAADwAAAAAAAAABAAAAAAAAAAEAAAAV" +
"AAAAcAAAAAIAAAAIAAAAxAAAAAMAAAADAAAA5AAAAAQAAAABAAAACAEAAAUAAAAFAAAAEAEAAAYA" +
"AAABAAAAOAEAAAEgAAAEAAAAWAEAAAMgAAAEAAAAwgEAAAIgAAAVAAAA1AEAAAQgAAACAAAAbQMA" +
"AAAgAAABAAAAfAMAAAMQAAACAAAAlAMAAAYgAAABAAAApAMAAAAQAAABAAAAtAMAAA==");


  public static void run() {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest(new SubTransform());
  }

  public static void doTest(SubTransform t) {
    t.sayHi();
    t.sayHi();
    t.sayHi();
    Redefinition.doMultiStructuralClassRedefinition(
        new Redefinition.CommonClassDefinition(SubTransform.class, null, SUBTRANSFORM_DEX_BYTES),
        new Redefinition.CommonClassDefinition(Transform.class, null, TRANSFORM_DEX_BYTES));
    t.sayHi();
    t.sayHi();
    t.sayHi();
  }
}
