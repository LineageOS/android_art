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
public class Test1996 {

  public static class SuperTransform {
    public String hiValue = "Hi";
    public String sayHi() {
      return this.hiValue;
    }
  }
  public static final class Transform extends SuperTransform {
    public void PostTransform() { }
    public String sayHiTwice(Runnable run) {
      run.run();
      return "super: " + super.sayHi() + " this: " + sayHi();
    }
  }

  /**
   * base64 encoded class/dex file for
   * public static final class Transform extends SuperTransform {
   *   public String myGreeting;
   *   public void PostTransform() {
   *     myGreeting = "SALUTATIONS";
   *   }
   *   public String sayHiTwice(Runnable run) {
   *     run.run();
   *     return "super: " + super.sayHi() + " and then this: " + sayHi();
   *   }
   *   public String sayHi() {
   *     return myGreeting;
   *   }
   * }
   */
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
"ZGV4CjAzNQAO4Dwurw97RcUtfH7np7S5RR8gsJYOfmeABQAAcAAAAHhWNBIAAAAAAAAAALwEAAAc" +
"AAAAcAAAAAkAAADgAAAABAAAAAQBAAABAAAANAEAAAoAAAA8AQAAAQAAAIwBAADUAwAArAEAAHYC" +
"AACIAgAAkAIAAJMCAACXAgAAtgIAANACAADgAgAABAMAACQDAAA6AwAATgMAAGkDAAB4AwAAhQMA" +
"AJQDAACfAwAAogMAAK8DAAC3AwAAwwMAAMkDAADOAwAA1QMAAOEDAADqAwAA9AMAAPsDAAAEAAAA" +
"BQAAAAYAAAAHAAAACAAAAAkAAAAKAAAACwAAABAAAAACAAAABgAAAAAAAAADAAAABgAAAGgCAAAD" +
"AAAABwAAAHACAAAQAAAACAAAAAAAAAABAAYAEwAAAAAAAwABAAAAAAAAABYAAAABAAMAAQAAAAEA" +
"AwAMAAAAAQAAABYAAAABAAEAFwAAAAUAAwAVAAAABwADAAEAAAAHAAIAEgAAAAcAAAAZAAAAAQAA" +
"ABEAAAAAAAAAAAAAAA4AAACsBAAAggQAAAAAAAACAAEAAAAAAFsCAAADAAAAVBAAABEAAAAFAAIA" +
"AgAAAF8CAAAlAAAAchAGAAQAbxABAAMADARuEAQAAwAMACIBBwBwEAcAAQAaAhgAbiAIACEAbiAI" +
"AEEAGgQAAG4gCABBAG4gCAABAG4QCQABAAwEEQQAAAEAAQABAAAAUgIAAAQAAABwEAAAAAAOAAIA" +
"AQAAAAAAVgIAAAUAAAAaAA0AWxAAAA4ACgAOAA0ADksAFAAOABABAA48AAAAAAEAAAAFAAAAAQAA" +
"AAYAECBhbmQgdGhlbiB0aGlzOiAABjxpbml0PgABTAACTEwAHUxhcnQvVGVzdDE5OTYkU3VwZXJU" +
"cmFuc2Zvcm07ABhMYXJ0L1Rlc3QxOTk2JFRyYW5zZm9ybTsADkxhcnQvVGVzdDE5OTY7ACJMZGFs" +
"dmlrL2Fubm90YXRpb24vRW5jbG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90YXRpb24vSW5uZXJD" +
"bGFzczsAFExqYXZhL2xhbmcvUnVubmFibGU7ABJMamF2YS9sYW5nL1N0cmluZzsAGUxqYXZhL2xh" +
"bmcvU3RyaW5nQnVpbGRlcjsADVBvc3RUcmFuc2Zvcm0AC1NBTFVUQVRJT05TAA1UZXN0MTk5Ni5q" +
"YXZhAAlUcmFuc2Zvcm0AAVYAC2FjY2Vzc0ZsYWdzAAZhcHBlbmQACm15R3JlZXRpbmcABG5hbWUA" +
"A3J1bgAFc2F5SGkACnNheUhpVHdpY2UAB3N1cGVyOiAACHRvU3RyaW5nAAV2YWx1ZQB2fn5EOHsi" +
"Y29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwibWluLWFwaSI6MSwic2hhLTEiOiI2MGRhNGQ2N2Iz" +
"ODFjNDI0Njc3NTdjNDlmYjZlNTU3NTZkODhhMmYzIiwidmVyc2lvbiI6IjEuNy4xMi1kZXYifQAC" +
"AwEaGAICBAIRBBkUFw8AAQEDAAECgYAEoAQDAbgEAQGsAwEBxAMAAAAAAAACAAAAcwQAAHkEAACg" +
"BAAAAAAAAAAAAAAAAAAAEAAAAAAAAAABAAAAAAAAAAEAAAAcAAAAcAAAAAIAAAAJAAAA4AAAAAMA" +
"AAAEAAAABAEAAAQAAAABAAAANAEAAAUAAAAKAAAAPAEAAAYAAAABAAAAjAEAAAEgAAAEAAAArAEA" +
"AAMgAAAEAAAAUgIAAAEQAAACAAAAaAIAAAIgAAAcAAAAdgIAAAQgAAACAAAAcwQAAAAgAAABAAAA" +
"ggQAAAMQAAACAAAAnAQAAAYgAAABAAAArAQAAAAQAAABAAAAvAQAAA==");

  public static void run() {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest(new Transform());
  }

  public static void doTest(final Transform t) {
    System.out.println(t.sayHiTwice(() -> { System.out.println("Not doing anything"); }));
    System.out.println(t.sayHiTwice(
      () -> {
        System.out.println("Redefining calling class");
        Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
        t.PostTransform();
      }));
    System.out.println(t.sayHiTwice(() -> { System.out.println("Not doing anything"); }));
  }
}
