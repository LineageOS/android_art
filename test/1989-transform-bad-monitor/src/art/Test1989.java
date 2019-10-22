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
public class Test1989 {

  static class Transform {
    public void sayHi(Object l_first, Object l_second) {
      System.out.println("hello without locks");
    }
  }

  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public void sayHi(Object l_first, Object l_second) {
   *    monitor-enter l_first
   *    monitor-enter l_second
   *    System.out.println("Goodbye before unlock");
   *    monitor-exit l_second
   *    System.out.println("Goodbye after unlock");
   *   }
   * }
   */
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
"yv66vgADAC0AHgEAFEdvb2RieWUgYWZ0ZXIgdW5sb2NrDAAYAB0BABBqYXZhL2xhbmcvT2JqZWN0" +
"AQAGPGluaXQ+BwADDAAEAAkHABEBABZhcnQvVGVzdDE5ODkkVHJhbnNmb3JtAQADKClWBwAVAQAE" +
"Q29kZQgAHAkACgACAQANVGVzdDE5ODkuamF2YQEAClNvdXJjZUZpbGUMABIAGwEAE2phdmEvaW8v" +
"UHJpbnRTdHJlYW0BAAdwcmludGxuCgAFAAYBAAVzYXlIaQEAEGphdmEvbGFuZy9TeXN0ZW0IAAEK" +
"AAcAEAEAA291dAcACAEAJyhMamF2YS9sYW5nL09iamVjdDtMamF2YS9sYW5nL09iamVjdDspVgEA" +
"FShMamF2YS9sYW5nL1N0cmluZzspVgEAFUdvb2RieWUgYmVmb3JlIHVubG9jawEAFUxqYXZhL2lv" +
"L1ByaW50U3RyZWFtOwAgABkABQAAAAAAAgAAAAQACQABAAsAAAARAAEAAQAAAAUqtwATsQAAAAAA" +
"AQAUABoAAQALAAAAIwACAAMAAAAXK8IswrIADRIMtgAXLMOyAA0SFrYAF7EAAAAAAAEADwAAAAIA" +
"Dg==");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
"ZGV4CjAzNQB5oAZwVUwJoMSgbr1BNffRcXjpPMVhYzgYBAAAcAAAAHhWNBIAAAAAAAAAAFQDAAAW" +
"AAAAcAAAAAkAAADIAAAAAwAAAOwAAAABAAAAEAEAAAQAAAAYAQAAAQAAADgBAADAAgAAWAEAAFgB" +
"AABgAQAAdgEAAI0BAACnAQAAtwEAANsBAAD7AQAAEgIAACYCAAA6AgAATgIAAF0CAABoAgAAawIA" +
"AG8CAAB0AgAAgQIAAIcCAACMAgAAlQIAAJwCAAADAAAABAAAAAUAAAAGAAAABwAAAAgAAAAJAAAA" +
"CgAAAA0AAAANAAAACAAAAAAAAAAPAAAACAAAAKQCAAAOAAAACAAAAKwCAAAHAAQAEgAAAAAAAAAA" +
"AAAAAAABABQAAAAEAAIAEwAAAAUAAAAAAAAAAAAAAAAAAAAFAAAAAAAAAAsAAADYAgAARAMAAAAA" +
"AAAGPGluaXQ+ABRHb29kYnllIGFmdGVyIHVubG9jawAVR29vZGJ5ZSBiZWZvcmUgdW5sb2NrABhM" +
"YXJ0L1Rlc3QxOTg5JFRyYW5zZm9ybTsADkxhcnQvVGVzdDE5ODk7ACJMZGFsdmlrL2Fubm90YXRp" +
"b24vRW5jbG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90YXRpb24vSW5uZXJDbGFzczsAFUxqYXZh" +
"L2lvL1ByaW50U3RyZWFtOwASTGphdmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsA" +
"EkxqYXZhL2xhbmcvU3lzdGVtOwANVGVzdDE5ODkuamF2YQAJVHJhbnNmb3JtAAFWAAJWTAADVkxM" +
"AAthY2Nlc3NGbGFncwAEbmFtZQADb3V0AAdwcmludGxuAAVzYXlIaQAFdmFsdWUAAAIAAAAFAAUA" +
"AQAAAAYAAgMCEAQIERcMAgIBFRgBAAAAAAAAAAAAAAACAAAAuwIAALICAADMAgAAAAAAAAAAAAAA" +
"AAAABgAOAAgCAAAOHh54H3gAAAEAAQABAAAA6AIAAAQAAABwEAMAAAAOAAUAAwACAAAA7AIAABIA" +
"AAAdAx0EYgAAABoBAgBuIAIAEAAeBGIDAAAaBAEAbiACAEMADgAAAAEBAICABPgFAQGQBgAAEAAA" +
"AAAAAAABAAAAAAAAAAEAAAAWAAAAcAAAAAIAAAAJAAAAyAAAAAMAAAADAAAA7AAAAAQAAAABAAAA" +
"EAEAAAUAAAAEAAAAGAEAAAYAAAABAAAAOAEAAAIgAAAWAAAAWAEAAAEQAAACAAAApAIAAAQgAAAC" +
"AAAAsgIAAAMQAAADAAAAxAIAAAYgAAABAAAA2AIAAAMgAAACAAAA6AIAAAEgAAACAAAA+AIAAAAg" +
"AAABAAAARAMAAAAQAAABAAAAVAMAAA==");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest(new Transform());
  }

  public static void doTest(Transform t) throws Exception {
    Object a = new Object();
    Object b = new Object();
    t.sayHi(a, b);
    Redefinition.doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
    try {
      t.sayHi(a, b);
    } catch (Throwable e) {
      System.out.println("Got exception of type " + e.getClass());
    }
    System.out.println("Make sure locks aren't held");
    Thread thr = new Thread(() -> {
      synchronized(a) {
        synchronized (b) {
          System.out.println("Locks are good.");
        }
      }
    });
    thr.start();
    thr.join();
  }
}
