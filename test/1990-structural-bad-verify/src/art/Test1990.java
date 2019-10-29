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
public class Test1990 {

  static class Transform {
    public static void saySomething() {
      System.out.println("hello");
    }
  }

  /**
   * base64 encoded class/dex file for
   * static class Transform {
   *   public static void saySomething() {
   *    System.out.println("I say hello and " + sayGoodbye());
   *   }
   *   public static String sayGoodbye() {
   *    return "you say goodbye!";
   *   }
   * }
   */
  // NB The actual dex codes are as follows. This is an explanation of the error this test checks.
  //
  // The exact order of instructions is important. Notice the 'invoke-static sayGoodbye'
  // (instruction 0002) dominates the rest of the block. During the first (runnable) verification
  // step the verifier will first check and verify there are no hard-failures in this class. Next it
  // will realize it cannot find the sayGoodbye method on the loaded & resolved Transform class.
  // This is (correctly) recognized as a soft-verification failure but then the verifier decides the
  // rest of the method is dead-code. This means the verifier will not perform any of the
  // soft-failure checks on the rest of the method (since control would never reach there).
  //
  // Later after performing the redefinition we do a reverify. At this time we held an exclusive
  // mutator-lock though so it cannot resolve classes and will not add anything to the dex-cache.
  // Here we can get past instruction 0002 and successfully determine the rest of the function is
  // fine. In the process we filled in the methods into the dex-cache but not the classes. This
  // caused this test to crash when run through the interpreter.
  //
  //     #2              : (in Lart/Test1990$Transform;)
  //       name          : 'saySomething'
  //       type          : '()V'
  //       access        : 0x0009 (PUBLIC STATIC)
  //       code          -
  //       registers     : 4
  //       ins           : 0
  //       outs          : 2
  //       insns size    : 27 16-bit code units
  // 0001d0:                                        |[0001d0] art.Test1990$Transform.saySomething:()V
  // 0001e0: 6200 0000                              |0000: sget-object v0, Ljava/lang/System;.out:Ljava/io/PrintStream; // field@0000
  // 0001e4: 7100 0100 0000                         |0002: invoke-static {}, Lart/Test1990$Transform;.sayGoodbye:()Ljava/lang/String; // method@0001
  // 0001ea: 0c01                                   |0005: move-result-object v1
  // 0001ec: 2202 0700                              |0006: new-instance v2, Ljava/lang/StringBuilder; // type@0007
  // 0001f0: 7010 0500 0200                         |0008: invoke-direct {v2}, Ljava/lang/StringBuilder;.<init>:()V // method@0005
  // 0001f6: 1a03 0100                              |000b: const-string v3, "I say hello and " // string@0001
  // 0001fa: 6e20 0600 3200                         |000d: invoke-virtual {v2, v3}, Ljava/lang/StringBuilder;.append:(Ljava/lang/String;)Ljava/lang/StringBuilder; // method@0006
  // 000200: 6e20 0600 1200                         |0010: invoke-virtual {v2, v1}, Ljava/lang/StringBuilder;.append:(Ljava/lang/String;)Ljava/lang/StringBuilder; // method@0006
  // 000206: 6e10 0700 0200                         |0013: invoke-virtual {v2}, Ljava/lang/StringBuilder;.toString:()Ljava/lang/String; // method@0007
  // 00020c: 0c01                                   |0016: move-result-object v1
  // 00020e: 6e20 0300 1000                         |0017: invoke-virtual {v0, v1}, Ljava/io/PrintStream;.println:(Ljava/lang/String;)V // method@0003
  // 000214: 0e00                                   |001a: return-void
  //       catches       : (none)
  //       positions     :
  //         0x0000 line=5
  //         0x001a line=6
  //       locals        :

  //   Virtual methods   -
  //   source_file_idx   : 13 (Test1990.java)
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
"ZGV4CjAzNQCV0LekDslEGFglxYgCw7HSyxVegIDjERswBQAAcAAAAHhWNBIAAAAAAAAAAGwEAAAc" +
"AAAAcAAAAAoAAADgAAAABAAAAAgBAAABAAAAOAEAAAgAAABAAQAAAQAAAIABAACQAwAAoAEAAC4C" +
"AAA2AgAASAIAAEsCAABPAgAAaQIAAHkCAACdAgAAvQIAANQCAADoAgAA/AIAABcDAAArAwAAOgMA" +
"AEUDAABIAwAATAMAAFkDAABhAwAAZwMAAGwDAAB1AwAAgQMAAI8DAACZAwAAoAMAALIDAAAEAAAA" +
"BQAAAAYAAAAHAAAACAAAAAkAAAAKAAAACwAAAAwAAAAPAAAAAgAAAAYAAAAAAAAAAwAAAAcAAAAo" +
"AgAADwAAAAkAAAAAAAAAEAAAAAkAAAAoAgAACAAEABQAAAAAAAIAAAAAAAAAAAAWAAAAAAACABcA" +
"AAAEAAMAFQAAAAUAAgAAAAAABwACAAAAAAAHAAEAEgAAAAcAAAAYAAAAAAAAAAAAAAAFAAAAAAAA" +
"AA0AAABcBAAAOQQAAAAAAAABAAAAAAAAABoCAAADAAAAGgAaABEAAAABAAEAAQAAABYCAAAEAAAA" +
"cBAEAAAADgAEAAAAAgAAAB4CAAAbAAAAYgAAAHEAAQAAAAwBIgIHAHAQBQACABoDAQBuIAYAMgBu" +
"IAYAEgBuEAcAAgAMAW4gAwAQAA4AAwAOAAgADgAFAA4BGg8AAAAAAQAAAAYABjxpbml0PgAQSSBz" +
"YXkgaGVsbG8gYW5kIAABTAACTEwAGExhcnQvVGVzdDE5OTAkVHJhbnNmb3JtOwAOTGFydC9UZXN0" +
"MTk5MDsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3Rh" +
"dGlvbi9Jbm5lckNsYXNzOwAVTGphdmEvaW8vUHJpbnRTdHJlYW07ABJMamF2YS9sYW5nL09iamVj" +
"dDsAEkxqYXZhL2xhbmcvU3RyaW5nOwAZTGphdmEvbGFuZy9TdHJpbmdCdWlsZGVyOwASTGphdmEv" +
"bGFuZy9TeXN0ZW07AA1UZXN0MTk5MC5qYXZhAAlUcmFuc2Zvcm0AAVYAAlZMAAthY2Nlc3NGbGFn" +
"cwAGYXBwZW5kAARuYW1lAANvdXQAB3ByaW50bG4ACnNheUdvb2RieWUADHNheVNvbWV0aGluZwAI" +
"dG9TdHJpbmcABXZhbHVlABB5b3Ugc2F5IGdvb2RieWUhAHZ+fkQ4eyJjb21waWxhdGlvbi1tb2Rl" +
"IjoiZGVidWciLCJtaW4tYXBpIjoxLCJzaGEtMSI6IjYwZGE0ZDY3YjM4MWM0MjQ2Nzc1N2M0OWZi" +
"NmU1NTc1NmQ4OGEyZjMiLCJ2ZXJzaW9uIjoiMS43LjEyLWRldiJ9AAICARkYAQIDAhEECBMXDgAA" +
"AwAAgIAEuAMBCaADAQnQAwAAAAAAAgAAACoEAAAwBAAAUAQAAAAAAAAAAAAAAAAAABAAAAAAAAAA" +
"AQAAAAAAAAABAAAAHAAAAHAAAAACAAAACgAAAOAAAAADAAAABAAAAAgBAAAEAAAAAQAAADgBAAAF" +
"AAAACAAAAEABAAAGAAAAAQAAAIABAAABIAAAAwAAAKABAAADIAAAAwAAABYCAAABEAAAAQAAACgC" +
"AAACIAAAHAAAAC4CAAAEIAAAAgAAACoEAAAAIAAAAQAAADkEAAADEAAAAgAAAEwEAAAGIAAAAQAA" +
"AFwEAAAAEAAAAQAAAGwEAAA=");



  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest(new Transform());
  }

  public static void doTest(Transform t) throws Exception {
    Transform.saySomething();
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    Transform.saySomething();
  }
}
