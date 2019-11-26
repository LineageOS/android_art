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
public class Test1999 {

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
   * public static class Transform {
   *   private int count;
   *   public String getGreeting() {
   *     return "Hello (Transform called " + incrCount() + " times)";
   *   }
   *   protected int incrCount() {
   *     return ++count;
   *   }
   * }
   */
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
"ZGV4CjAzNQAwwbMpPdPdWkU+6UJnvqa7v4VBdcuq2vkMBQAAcAAAAHhWNBIAAAAAAAAAAEgEAAAa" +
"AAAAcAAAAAkAAADYAAAABQAAAPwAAAABAAAAOAEAAAgAAABAAQAAAQAAAIABAABsAwAAoAEAADoC" +
"AABDAgAASwIAAGUCAABoAgAAawIAAG8CAABzAgAAjQIAAJ0CAADBAgAA4QIAAPUCAAAJAwAAJAMA" +
"ADMDAAA+AwAAQQMAAE4DAABWAwAAXQMAAGoDAAB1AwAAewMAAIUDAACMAwAAAwAAAAcAAAAIAAAA" +
"CQAAAAoAAAALAAAADAAAAA0AAAAQAAAAAwAAAAAAAAAAAAAABAAAAAYAAAAAAAAABQAAAAcAAAAs" +
"AgAABgAAAAcAAAA0AgAAEAAAAAgAAAAAAAAAAQAAABMAAAABAAQAAQAAAAEAAQAUAAAAAQAAABUA" +
"AAAFAAQAAQAAAAcABAABAAAABwACABIAAAAHAAMAEgAAAAcAAQAXAAAAAQAAAAEAAAAFAAAAAAAA" +
"AA4AAAA4BAAAEwQAAAAAAAACAAEAAAAAACgCAAAHAAAAUhAAANgAAAFZEAAADwAAAAQAAQACAAAA" +
"JAIAABsAAABuEAIAAwAKACIBBwBwEAQAAQAaAgIAbiAGACEAbiAFAAEAGgAAAG4gBgABAG4QBwAB" +
"AAwAEQAAAAEAAQABAAAAIAIAAAQAAABwEAMAAAAOAAMADgAGAA4ACQAOAAEAAAAAAAAAAQAAAAYA" +
"ByB0aW1lcykABjxpbml0PgAYSGVsbG8gKFRyYW5zZm9ybSBjYWxsZWQgAAFJAAFMAAJMSQACTEwA" +
"GExhcnQvVGVzdDE5OTkkVHJhbnNmb3JtOwAOTGFydC9UZXN0MTk5OTsAIkxkYWx2aWsvYW5ub3Rh" +
"dGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwASTGph" +
"dmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAGUxqYXZhL2xhbmcvU3RyaW5nQnVp" +
"bGRlcjsADVRlc3QxOTk5LmphdmEACVRyYW5zZm9ybQABVgALYWNjZXNzRmxhZ3MABmFwcGVuZAAF" +
"Y291bnQAC2dldEdyZWV0aW5nAAlpbmNyQ291bnQABG5hbWUACHRvU3RyaW5nAAV2YWx1ZQB2fn5E" +
"OHsiY29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwibWluLWFwaSI6MSwic2hhLTEiOiI2MGRhNGQ2" +
"N2IzODFjNDI0Njc3NTdjNDlmYjZlNTU3NTZkODhhMmYzIiwidmVyc2lvbiI6IjEuNy4xMi1kZXYi" +
"fQACAwEYGAICBAIRBAkWFw8AAQECAAIAgYAEiAQBAcADAQSgAwAAAAAAAgAAAAQEAAAKBAAALAQA" +
"AAAAAAAAAAAAAAAAABAAAAAAAAAAAQAAAAAAAAABAAAAGgAAAHAAAAACAAAACQAAANgAAAADAAAA" +
"BQAAAPwAAAAEAAAAAQAAADgBAAAFAAAACAAAAEABAAAGAAAAAQAAAIABAAABIAAAAwAAAKABAAAD" +
"IAAAAwAAACACAAABEAAAAgAAACwCAAACIAAAGgAAADoCAAAEIAAAAgAAAAQEAAAAIAAAAQAAABME" +
"AAADEAAAAgAAACgEAAAGIAAAAQAAADgEAAAAEAAAAQAAAEgEAAA=");


  public static void run() {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest(new SubTransform());
  }

  public static void doTest(SubTransform t) {
    t.sayHi();
    t.sayHi();
    t.sayHi();
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    t.sayHi();
  }
}
