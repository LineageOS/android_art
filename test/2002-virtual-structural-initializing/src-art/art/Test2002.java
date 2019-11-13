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

import dalvik.system.InMemoryDexClassLoader;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Base64;
import java.util.concurrent.CountDownLatch;
import java.util.function.Supplier;

public class Test2002 {
  public static final CountDownLatch start_latch = new CountDownLatch(1);
  public static final CountDownLatch finish_latch = new CountDownLatch(1);
  public static class Transform {
    public Transform() { }

    public String sayHi() {
      return "Hi";
    }
  }

  /**
   * base64 encoded class/dex file for
   * public static class Transform {
   *   public String greeting;
   *
   *   public Transform() {
   *     greeting = "Hello";
   *   }
   *   public String sayHi() {
   *     return greeting;
   *   }
   * }
   */
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
"ZGV4CjAzNQBlpDFxr5PhCBfCyN+GZYuYQvSqtTEESU3oAwAAcAAAAHhWNBIAAAAAAAAAADADAAAS" +
"AAAAcAAAAAcAAAC4AAAAAgAAANQAAAABAAAA7AAAAAMAAAD0AAAAAQAAAAwBAAC8AgAALAEAAHAB" +
"AAB4AQAAfwEAAIIBAACcAQAArAEAANABAADwAQAABAIAABgCAAAnAgAAMgIAADUCAABCAgAATAIA" +
"AFICAABZAgAAYAIAAAMAAAAEAAAABQAAAAYAAAAHAAAACAAAAAsAAAACAAAABQAAAAAAAAALAAAA" +
"BgAAAAAAAAAAAAUADQAAAAAAAQAAAAAAAAAAAA8AAAAEAAEAAAAAAAAAAAABAAAABAAAAAAAAAAJ" +
"AAAAIAMAAP0CAAAAAAAAAgABAAAAAABqAQAAAwAAAFQQAAARAAAAAgABAAEAAABkAQAACAAAAHAQ" +
"AgABABoAAQBbEAAADgAGAA48SwAKAA4AAAAGPGluaXQ+AAVIZWxsbwABTAAYTGFydC9UZXN0MjAw" +
"MiRUcmFuc2Zvcm07AA5MYXJ0L1Rlc3QyMDAyOwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xvc2lu" +
"Z0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xhc3M7ABJMamF2YS9sYW5nL09iamVj" +
"dDsAEkxqYXZhL2xhbmcvU3RyaW5nOwANVGVzdDIwMDIuamF2YQAJVHJhbnNmb3JtAAFWAAthY2Nl" +
"c3NGbGFncwAIZ3JlZXRpbmcABG5hbWUABXNheUhpAAV2YWx1ZQCLAX5+RDh7ImNvbXBpbGF0aW9u" +
"LW1vZGUiOiJkZWJ1ZyIsImhhcy1jaGVja3N1bXMiOmZhbHNlLCJtaW4tYXBpIjoxLCJzaGEtMSI6" +
"ImY2MmI4Y2U2YTA1OTAwNTRlZjM0YTFhZWRlNzBiNDY2NjhlOGI0OWYiLCJ2ZXJzaW9uIjoiMi4w" +
"LjEtZGV2In0AAgIBEBgBAgMCDAQJDhcKAAEBAQABAIGABMQCAQGsAgAAAAAAAAACAAAA7gIAAPQC" +
"AAAUAwAAAAAAAAAAAAAAAAAADwAAAAAAAAABAAAAAAAAAAEAAAASAAAAcAAAAAIAAAAHAAAAuAAA" +
"AAMAAAACAAAA1AAAAAQAAAABAAAA7AAAAAUAAAADAAAA9AAAAAYAAAABAAAADAEAAAEgAAACAAAA" +
"LAEAAAMgAAACAAAAZAEAAAIgAAASAAAAcAEAAAQgAAACAAAA7gIAAAAgAAABAAAA/QIAAAMQAAAC" +
"AAAAEAMAAAYgAAABAAAAIAMAAAAQAAABAAAAMAMAAA==");

  /*
   * base64 encoded class/dex file for
    package art;
    import java.util.function.Supplier;
    import java.util.concurrent.CountDownLatch;

    public class SubTransform extends art.Test2002.Transform implements Supplier<String> {
      public static final String staticId;
      static {
        String res = null;
        try {
          Test2002.start_latch.countDown();
          Test2002.finish_latch.await();
          res = "Initialized Static";
        } catch (Exception e) {
          res = e.toString();
        }
        staticId = res;
      }
      public SubTransform() {
        super();
      }
      public String get() {
        return SubTransform.staticId + " " + sayHi();
      }
    }
   */
  private static final byte[] SUB_DEX_BYTES =
      Base64.getDecoder()
          .decode(
"ZGV4CjAzNQB0BhXQtGTKXAGE/UzeevPgeNK7UrQJRJkoBgAAcAAAAHhWNBIAAAAAAAAAAGQFAAAf" +
"AAAAcAAAAAsAAADsAAAABAAAABgBAAADAAAASAEAAAwAAABgAQAAAQAAAMABAABIBAAA4AEAAM4C" +
"AADRAgAA2wIAAOMCAADnAgAA+wIAAP4CAAACAwAAFgMAADADAABAAwAAXwMAAHYDAACKAwAAngMA" +
"ALkDAADgAwAA/wMAAB4EAAAxBAAANAQAADwEAABDBAAATgQAAFwEAABhBAAAaAQAAHUEAAB/BAAA" +
"iQQAAJAEAAAHAAAACAAAAAkAAAAKAAAACwAAAAwAAAANAAAADgAAAA8AAAAQAAAAEwAAAAUAAAAF" +
"AAAAAAAAAAUAAAAGAAAAAAAAAAYAAAAHAAAAyAIAABMAAAAKAAAAAAAAAAAABgAbAAAAAgAIABcA" +
"AAACAAgAGgAAAAAAAwABAAAAAAADAAIAAAAAAAAAGAAAAAAAAQAYAAAAAAABABkAAAABAAMAAgAA" +
"AAQAAQAcAAAABwADAAIAAAAHAAIAFAAAAAcAAQAcAAAACAADABUAAAAIAAMAFgAAAAAAAAABAAAA" +
"AQAAAMACAAASAAAAVAUAACwFAAAAAAAAAgABAAEAAAC1AgAABQAAAG4QAwABAAwAEQAAAAQAAQAC" +
"AAAAuQIAABsAAABiAAAAbhAEAAMADAEiAgcAcBAHAAIAbiAIAAIAGgAAAG4gCAACAG4gCAASAG4Q" +
"CQACAAwAEQAAAAEAAAABAAEApAIAABYAAAAAAGIAAgBuEAsAAABiAAEAbhAKAAAAGgAEACgGDQBu" +
"EAYAAAAMAGkAAAAOAAEAAAAMAAEAAQEEDgEAAQABAAAAsAIAAAQAAABwEAUAAAAOAAgADh9aWi8b" +
"HkwtABMADjwABQAOABYADgAAAAABAAAACQAAAAEAAAAGAAEgAAg8Y2xpbml0PgAGPGluaXQ+AAI+" +
"OwASSW5pdGlhbGl6ZWQgU3RhdGljAAFMAAJMTAASTGFydC9TdWJUcmFuc2Zvcm07ABhMYXJ0L1Rl" +
"c3QyMDAyJFRyYW5zZm9ybTsADkxhcnQvVGVzdDIwMDI7AB1MZGFsdmlrL2Fubm90YXRpb24vU2ln" +
"bmF0dXJlOwAVTGphdmEvbGFuZy9FeGNlcHRpb247ABJMamF2YS9sYW5nL09iamVjdDsAEkxqYXZh" +
"L2xhbmcvU3RyaW5nOwAZTGphdmEvbGFuZy9TdHJpbmdCdWlsZGVyOwAlTGphdmEvdXRpbC9jb25j" +
"dXJyZW50L0NvdW50RG93bkxhdGNoOwAdTGphdmEvdXRpbC9mdW5jdGlvbi9TdXBwbGllcjsAHUxq" +
"YXZhL3V0aWwvZnVuY3Rpb24vU3VwcGxpZXI8ABFTdWJUcmFuc2Zvcm0uamF2YQABVgAGYXBwZW5k" +
"AAVhd2FpdAAJY291bnREb3duAAxmaW5pc2hfbGF0Y2gAA2dldAAFc2F5SGkAC3N0YXJ0X2xhdGNo" +
"AAhzdGF0aWNJZAAIdG9TdHJpbmcABXZhbHVlAIsBfn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRl" +
"YnVnIiwiaGFzLWNoZWNrc3VtcyI6ZmFsc2UsIm1pbi1hcGkiOjEsInNoYS0xIjoiZjYyYjhjZTZh" +
"MDU5MDA1NGVmMzRhMWFlZGU3MGI0NjY2OGU4YjQ5ZiIsInZlcnNpb24iOiIyLjAuMS1kZXYifQAC" +
"AwEdHAQXCBcRFw0XAwEAAgIAGQCIgATEBAGBgASMBQLBIOADAQH8AwAAAAAAAQAAAB4FAABMBQAA" +
"AAAAAAAAAAAAAAAAEAAAAAAAAAABAAAAAAAAAAEAAAAfAAAAcAAAAAIAAAALAAAA7AAAAAMAAAAE" +
"AAAAGAEAAAQAAAADAAAASAEAAAUAAAAMAAAAYAEAAAYAAAABAAAAwAEAAAEgAAAEAAAA4AEAAAMg" +
"AAAEAAAApAIAAAEQAAACAAAAwAIAAAIgAAAfAAAAzgIAAAQgAAABAAAAHgUAAAAgAAABAAAALAUA" +
"AAMQAAACAAAASAUAAAYgAAABAAAAVAUAAAAQAAABAAAAZAUAAA==");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static Supplier<String> mkTransform() {
    try {
      return (Supplier<String>)
          (new InMemoryDexClassLoader(
                  ByteBuffer.wrap(SUB_DEX_BYTES), Test2002.class.getClassLoader())
              .loadClass("art.SubTransform")
              .newInstance());
    } catch (Exception e) {
      return () -> {
        return e.toString();
      };
    }
  }

  public static void doTest() throws Exception {
    Thread t = new Thread(() -> {
      Supplier<String> s = mkTransform();
      System.out.println(s.get());
    });
    t.start();
    start_latch.await();
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    finish_latch.countDown();
    t.join();
  }
}
