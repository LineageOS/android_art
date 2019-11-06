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

public class Test1998 {

  public static class SuperTransform {
    public static String greeting = "Hello";
  }

  // The class we will be transforming.
  public static class Transform extends SuperTransform { }

  // public static class Transform extends SuperTransform {
  //   public static String greeting;
  // }
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
"ZGV4CjAzNQCYmnoWz4BqygrZQM4zf/mJ/25+dM86MHKAAwAAcAAAAHhWNBIAAAAAAAAAAMgCAAAP" +
"AAAAcAAAAAcAAACsAAAAAQAAAMgAAAABAAAA1AAAAAIAAADcAAAAAQAAAOwAAAB0AgAADAEAACgB" +
"AAAwAQAATwEAAGkBAAB5AQAAnQEAAL0BAADRAQAA4AEAAOsBAADuAQAA+wEAAAUCAAALAgAAEgIA" +
"AAEAAAACAAAAAwAAAAQAAAAFAAAABgAAAAkAAAAJAAAABgAAAAAAAAABAAUACwAAAAAAAAAAAAAA" +
"AQAAAAAAAAABAAAAAQAAAAAAAAAAAAAABwAAALgCAACZAgAAAAAAAAEAAQABAAAAJAEAAAQAAABw" +
"EAAAAAAOAAUADgAGPGluaXQ+AB1MYXJ0L1Rlc3QxOTk4JFN1cGVyVHJhbnNmb3JtOwAYTGFydC9U" +
"ZXN0MTk5OCRUcmFuc2Zvcm07AA5MYXJ0L1Rlc3QxOTk4OwAiTGRhbHZpay9hbm5vdGF0aW9uL0Vu" +
"Y2xvc2luZ0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xhc3M7ABJMamF2YS9sYW5n" +
"L1N0cmluZzsADVRlc3QxOTk4LmphdmEACVRyYW5zZm9ybQABVgALYWNjZXNzRmxhZ3MACGdyZWV0" +
"aW5nAARuYW1lAAV2YWx1ZQB2fn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwibWluLWFw" +
"aSI6MSwic2hhLTEiOiI2MGRhNGQ2N2IzODFjNDI0Njc3NTdjNDlmYjZlNTU3NTZkODhhMmYzIiwi" +
"dmVyc2lvbiI6IjEuNy4xMi1kZXYifQACAwENGAICBAIKBAkMFwgBAAEAAAkBgYAEjAIAAAAAAAAA" +
"AgAAAIoCAACQAgAArAIAAAAAAAAAAAAAAAAAAA8AAAAAAAAAAQAAAAAAAAABAAAADwAAAHAAAAAC" +
"AAAABwAAAKwAAAADAAAAAQAAAMgAAAAEAAAAAQAAANQAAAAFAAAAAgAAANwAAAAGAAAAAQAAAOwA" +
"AAABIAAAAQAAAAwBAAADIAAAAQAAACQBAAACIAAADwAAACgBAAAEIAAAAgAAAIoCAAAAIAAAAQAA" +
"AJkCAAADEAAAAgAAAKgCAAAGIAAAAQAAALgCAAAAEAAAAQAAAMgCAAA=");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static void doTest() throws Exception {
    System.out.println(Transform.greeting);
    System.out.println(SuperTransform.greeting);
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    System.out.println(Transform.greeting);
    System.out.println(SuperTransform.greeting);
  }
}
