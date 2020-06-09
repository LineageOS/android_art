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
import java.lang.reflect.Method;

public class Test2035 {
  public static class Transform {
    public Transform() {}

    public native long getValue();
  }
  /*
   * base64 encoded class/dex file for
   * Base64 generated using:
   * % javac Test2035.java
   * % d8 Test2035\$Transform.class
   * % base64 classes.dex| sed 's:^:":' | sed 's:$:" +:'
   *
   * package art;
   * public static class Transform {
   *   public Transform() {
   *   }
   *   public native long getValue();
   *   public long nonNativeValue() {
   *     return 1337;
   *   };
   * }
   */
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQAIm/YHNPSI0ggIbrKz6Jg/IBYl2Kq0TXS8AwAAcAAAAHhWNBIAAAAAAAAAABADAAAQ"
                  + "AAAAcAAAAAcAAACwAAAAAgAAAMwAAAAAAAAAAAAAAAQAAADkAAAAAQAAAAQBAACYAgAAJAEAAGAB"
                  + "AABoAQAAawEAAIUBAACVAQAAuQEAANkBAADtAQAA/AEAAAcCAAAKAgAAFwIAACECAAAnAgAANwIA"
                  + "AD4CAAABAAAAAgAAAAMAAAAEAAAABQAAAAYAAAAJAAAAAQAAAAAAAAAAAAAACQAAAAYAAAAAAAAA"
                  + "AQABAAAAAAABAAAACwAAAAEAAAANAAAABQABAAAAAAABAAAAAQAAAAUAAAAAAAAABwAAAAADAADb"
                  + "AgAAAAAAAAMAAQAAAAAAVAEAAAMAAAAWADkFEAAAAAEAAQABAAAAWAEAAAQAAABwEAMAAAAOAA0A"
                  + "DgAJAA48AAAAAAY8aW5pdD4AAUoAGExhcnQvVGVzdDIwMzUkVHJhbnNmb3JtOwAOTGFydC9UZXN0"
                  + "MjAzNTsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3Rh"
                  + "dGlvbi9Jbm5lckNsYXNzOwASTGphdmEvbGFuZy9PYmplY3Q7AA1UZXN0MjAzNS5qYXZhAAlUcmFu"
                  + "c2Zvcm0AAVYAC2FjY2Vzc0ZsYWdzAAhnZXRWYWx1ZQAEbmFtZQAObm9uTmF0aXZlVmFsdWUABXZh"
                  + "bHVlAIsBfn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwiaGFzLWNoZWNrc3VtcyI6ZmFs"
                  + "c2UsIm1pbi1hcGkiOjEsInNoYS0xIjoiOGNkYTg3OGE1MjJiMjJkMWQ2YTljNGQ0MjY5M2Y0OTAw"
                  + "MjJmZTQ2YiIsInZlcnNpb24iOiIyLjIuMS1kZXYifQACAwEOGAICBAIKBAkMFwgAAAECAIGABLwC"
                  + "AYECAAEBpAIAAAAAAAAAAgAAAMwCAADSAgAA9AIAAAAAAAAAAAAAAAAAAA4AAAAAAAAAAQAAAAAA"
                  + "AAABAAAAEAAAAHAAAAACAAAABwAAALAAAAADAAAAAgAAAMwAAAAFAAAABAAAAOQAAAAGAAAAAQAA"
                  + "AAQBAAABIAAAAgAAACQBAAADIAAAAgAAAFQBAAACIAAAEAAAAGABAAAEIAAAAgAAAMwCAAAAIAAA"
                  + "AQAAANsCAAADEAAAAgAAAPACAAAGIAAAAQAAAAADAAAAEAAAAQAAABADAAA=");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static void doTest() throws Exception {
    LinkClassMethods(Transform.class);
    Transform t = new Transform();
    System.out.println("value is " + t.getValue());
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    System.out.println("value is " + t.getValue());
    System.out.println(
        "non-native value is " + Transform.class.getDeclaredMethod("nonNativeValue").invoke(t));
  }

  public static native void LinkClassMethods(Class<?> k);
}
