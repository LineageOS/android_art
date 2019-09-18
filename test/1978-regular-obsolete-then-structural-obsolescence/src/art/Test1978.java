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

// TODO Need version where non-structural obsolete method tries to touch a structural obsolete
// field.
public class Test1978 {

  // The class we will be transforming.
  static class Transform {
    static {
    }

    public static String sayHiName = " Alex";
    // Called whenever we do something.
    public static void somethingHappened1() {
      System.out.println("Running after op1 using normal definition");
    }

    public static void somethingHappened2() {
      System.out.println("Running after op2 using normal definition");
    }

    public static void sayHi(Runnable r1, Runnable r2) {
      System.out.println("hello" + sayHiName);
      r1.run();
      somethingHappened1();
      System.out.println("how do you do" + sayHiName);
      r2.run();
      somethingHappened2();
      System.out.println("goodbye" + sayHiName);
    }
  }

  // static class Transform {
  //   static {}
  //   public static String sayHiName;
  //   public static void somethingHappened1() {
  //     System.out.println("Running after op1 using non-structural redefinition");
  //   }
  //   public static void somethingHappened2() {
  //     System.out.println("Running after op2 using non-structural redefinition");
  //   }
  //   public static void sayHi(Runnable r1, Runnable r2) {
  //     System.out.println("TRANSFORMED_NON_STRUCTURAL hello" + sayHiName);
  //     r1.run();
  //     somethingHappened1();
  //     System.out.println("TRANSFORMED_NON_STRUCTURAL how do you do" + sayHiName);
  //     r2.run();
  //     somethingHappened2();
  //     System.out.println("TRANSFORMED_NON_STRUCTURAL goodbye" + sayHiName);
  //   }
  // }
  private static final byte[] NON_STRUCTURAL_DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQCrRWiiuda6GysXvxv0iJ+9HUcPBCTU/UVkBwAAcAAAAHhWNBIAAAAAAAAAAKAGAAAl"
                  + "AAAAcAAAAAsAAAAEAQAABQAAADABAAACAAAAbAEAAAsAAAB8AQAAAQAAANQBAABwBQAA9AEAAFQD"
                  + "AABeAwAAZgMAAGkDAABtAwAAhwMAAJcDAAC7AwAA2wMAAPIDAAAGBAAAHAQAADAEAABLBAAAXwQA"
                  + "AJQEAADJBAAA7QQAAA8FAAA5BQAASAUAAFMFAABWBQAAWgUAAF8FAABsBQAAdAUAAHoFAAB/BQAA"
                  + "iAUAAI0FAACUBQAAnwUAALMFAADHBQAA0QUAANgFAAAEAAAABQAAAAYAAAAHAAAACAAAAAkAAAAK"
                  + "AAAACwAAAAwAAAANAAAAFQAAAAIAAAAHAAAAAAAAAAMAAAAIAAAARAMAABUAAAAKAAAAAAAAABcA"
                  + "AAAKAAAATAMAABYAAAAKAAAARAMAAAAABwAfAAAACQAEABsAAAAAAAIAAAAAAAAAAgABAAAAAAAD"
                  + "AB4AAAAAAAIAIAAAAAAAAgAhAAAABAAEABwAAAAFAAIAAQAAAAYAAgAdAAAACAACAAEAAAAIAAEA"
                  + "GQAAAAgAAAAiAAAAAAAAAAAAAAAFAAAAAAAAABMAAACQBgAAXwYAAAAAAAAAAAAAAAAAABwDAAAB"
                  + "AAAADgAAAAEAAQABAAAAIAMAAAQAAABwEAYAAAAOAAYAAgACAAAAJAMAAFUAAABiAAEAYgEAACIC"
                  + "CABwEAgAAgAaAxEAbiAJADIAbiAJABIAbhAKAAIADAFuIAUAEAByEAcABABxAAMAAABiBAEAYgAA"
                  + "ACIBCABwEAgAAQAaAhIAbiAJACEAbiAJAAEAbhAKAAEADABuIAUABAByEAcABQBxAAQAAABiBAEA"
                  + "YgUAACIACABwEAgAAAAaARAAbiAJABAAbiAJAFAAbhAKAAAADAVuIAUAVAAOAAAAAgAAAAIAAAA3"
                  + "AwAACAAAAGIAAQAaAQ4AbiAFABAADgACAAAAAgAAADwDAAAIAAAAYgABABoBDwBuIAUAEAAOAAcA"
                  + "DgAGAA4AEAIAAA4BGA88PAEYDzw8ARgPAAoADngADQAOeAAAAAABAAAABwAAAAIAAAAGAAYACDxj"
                  + "bGluaXQ+AAY8aW5pdD4AAUwAAkxMABhMYXJ0L1Rlc3QxOTc4JFRyYW5zZm9ybTsADkxhcnQvVGVz"
                  + "dDE5Nzg7ACJMZGFsdmlrL2Fubm90YXRpb24vRW5jbG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90"
                  + "YXRpb24vSW5uZXJDbGFzczsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGphdmEvbGFuZy9PYmpl"
                  + "Y3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9sYW5n"
                  + "L1N0cmluZ0J1aWxkZXI7ABJMamF2YS9sYW5nL1N5c3RlbTsAM1J1bm5pbmcgYWZ0ZXIgb3AxIHVz"
                  + "aW5nIG5vbi1zdHJ1Y3R1cmFsIHJlZGVmaW5pdGlvbgAzUnVubmluZyBhZnRlciBvcDIgdXNpbmcg"
                  + "bm9uLXN0cnVjdHVyYWwgcmVkZWZpbml0aW9uACJUUkFOU0ZPUk1FRF9OT05fU1RSVUNUVVJBTCBn"
                  + "b29kYnllACBUUkFOU0ZPUk1FRF9OT05fU1RSVUNUVVJBTCBoZWxsbwAoVFJBTlNGT1JNRURfTk9O"
                  + "X1NUUlVDVFVSQUwgaG93IGRvIHlvdSBkbwANVGVzdDE5NzguamF2YQAJVHJhbnNmb3JtAAFWAAJW"
                  + "TAADVkxMAAthY2Nlc3NGbGFncwAGYXBwZW5kAARuYW1lAANvdXQAB3ByaW50bG4AA3J1bgAFc2F5"
                  + "SGkACXNheUhpTmFtZQASc29tZXRoaW5nSGFwcGVuZWQxABJzb21ldGhpbmdIYXBwZW5lZDIACHRv"
                  + "U3RyaW5nAAV2YWx1ZQB2fn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwibWluLWFwaSI6"
                  + "MSwic2hhLTEiOiJhODM1MmYyNTQ4ODUzNjJjY2Q4ZDkwOWQzNTI5YzYwMDk0ZGQ4OTZlIiwidmVy"
                  + "c2lvbiI6IjEuNi4yMC1kZXYifQACAgEjGAECAwIYBAgaFxQBAAUAAAkAiIAE9AMBgIAEiAQBCaAE"
                  + "AQncBQEJ/AUAAAAAAAAAAgAAAFAGAABWBgAAhAYAAAAAAAAAAAAAAAAAABAAAAAAAAAAAQAAAAAA"
                  + "AAABAAAAJQAAAHAAAAACAAAACwAAAAQBAAADAAAABQAAADABAAAEAAAAAgAAAGwBAAAFAAAACwAA"
                  + "AHwBAAAGAAAAAQAAANQBAAABIAAABQAAAPQBAAADIAAABQAAABwDAAABEAAAAgAAAEQDAAACIAAA"
                  + "JQAAAFQDAAAEIAAAAgAAAFAGAAAAIAAAAQAAAF8GAAADEAAAAgAAAIAGAAAGIAAAAQAAAJAGAAAA"
                  + "EAAAAQAAAKAGAAA=");

  // static class Transform {
  //   static {}
  //   // NB Due to the ordering of fields offset of sayHiName will change.
  //   public static String sayHiName;
  //   public static String sayByeName;
  //   public static String sayQuery;
  //   public static void somethingHappened1() {
  //     System.out.println("Running after op2 using structural redefinition");
  //     sayQuery = " this fine day";
  //   }
  //   public static void somethingHappened2() {
  //     System.out.println("Running after op2 using structural redefinition");
  //     sayByeName = " and good luck";
  //   }
  //   public static void doSayBye() {
  //     System.out.println("Goodbye" + sayByeName + " - Transformed");
  //   }
  //   public static void doQuery() {
  //     System.out.println("How do you do" + sayQuery + " - Transformed");
  //   }
  //   public static void doSayHi() {
  //     System.out.println("Hello" + sayHiName + " - Transformed");
  //   }
  //   public static void sayHi(Runnable r, Runnable r2) {
  //     doSayHi();
  //     r1.run();
  //     somethingHappened1();
  //     doQuery();
  //     r2.run();
  //     somethingHappened2();
  //     doSayBye();
  //   }
  // }
  private static final byte[] STRUCTURAL_DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQBcSGjP90G9cWx1TjBkAO5SCOfSe5sjsEAUCAAAcAAAAHhWNBIAAAAAAAAAAFAHAAAs"
                  + "AAAAcAAAAAsAAAAgAQAABQAAAEwBAAAEAAAAiAEAAA4AAACoAQAAAQAAABgCAADcBQAAOAIAABwE"
                  + "AAAsBAAAPAQAAEwEAABWBAAAXgQAAGcEAABuBAAAfQQAAIAEAACEBAAAngQAAK4EAADSBAAA8gQA"
                  + "AAkFAAAdBQAAMwUAAEcFAABiBQAAdgUAAKcFAAC2BQAAwQUAAMQFAADIBQAAzQUAANoFAADiBQAA"
                  + "6wUAAPUFAAD+BQAABAYAAAkGAAASBgAAFwYAACMGAAAqBgAANQYAAD8GAABTBgAAZwYAAHEGAAB4"
                  + "BgAACgAAAAsAAAAMAAAADQAAAA4AAAAPAAAAEAAAABEAAAASAAAAEwAAABcAAAAIAAAABwAAAAAA"
                  + "AAAJAAAACAAAAAwEAAAXAAAACgAAAAAAAAAZAAAACgAAABQEAAAYAAAACgAAAAwEAAAAAAcAIwAA"
                  + "AAAABwAlAAAAAAAHACYAAAAJAAQAIAAAAAAAAgADAAAAAAACAAQAAAAAAAIAHAAAAAAAAgAdAAAA"
                  + "AAACAB4AAAAAAAMAJAAAAAAAAgAnAAAAAAACACgAAAAEAAQAIQAAAAUAAgAEAAAABgACACIAAAAI"
                  + "AAIABAAAAAgAAQAbAAAACAAAACkAAAAAAAAAAAAAAAUAAAAAAAAAFQAAAEAHAAD/BgAAAAAAAAAA"
                  + "AAAAAAAA1AMAAAEAAAAOAAAAAQABAAEAAADYAwAABAAAAHAQCQAAAA4ABAAAAAIAAADcAwAAHgAA"
                  + "AGIAAwBiAQIAIgIIAHAQCwACABoDBwBuIAwAMgBuIAwAEgAaAQAAbiAMABIAbhANAAIADAFuIAgA"
                  + "EAAOAAQAAAACAAAA4wMAAB4AAABiAAMAYgEAACICCABwEAsAAgAaAwUAbiAMADIAbiAMABIAGgEA"
                  + "AG4gDAASAG4QDQACAAwBbiAIABAADgAEAAAAAgAAAOoDAAAeAAAAYgADAGIBAQAiAggAcBALAAIA"
                  + "GgMGAG4gDAAyAG4gDAASABoBAABuIAwAEgBuEA0AAgAMAW4gCAAQAA4AAgACAAEAAADxAwAAFgAA"
                  + "AHEABAAAAHIQCgAAAHEABgAAAHEAAgAAAHIQCgABAHEABwAAAHEAAwAAAA4AAgAAAAIAAAD+AwAA"
                  + "DAAAAGIAAwAaARQAbiAIABAAGgACAGkAAgAOAAIAAAACAAAABAQAAAwAAABiAAMAGgEUAG4gCAAQ"
                  + "ABoAAQBpAAAADgAHAA4ABgAOABgADgEdDwAVAA4BHQ8AGwAOAR0PAB4CAAAOPDw8PDw8PAANAA54"
                  + "SwARAA54SwAAAAEAAAAHAAAAAgAAAAYABgAOIC0gVHJhbnNmb3JtZWQADiBhbmQgZ29vZCBsdWNr"
                  + "AA4gdGhpcyBmaW5lIGRheQAIPGNsaW5pdD4ABjxpbml0PgAHR29vZGJ5ZQAFSGVsbG8ADUhvdyBk"
                  + "byB5b3UgZG8AAUwAAkxMABhMYXJ0L1Rlc3QxOTc4JFRyYW5zZm9ybTsADkxhcnQvVGVzdDE5Nzg7"
                  + "ACJMZGFsdmlrL2Fubm90YXRpb24vRW5jbG9zaW5nQ2xhc3M7AB5MZGFsdmlrL2Fubm90YXRpb24v"
                  + "SW5uZXJDbGFzczsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGphdmEvbGFuZy9PYmplY3Q7ABRM"
                  + "amF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9sYW5nL1N0cmlu"
                  + "Z0J1aWxkZXI7ABJMamF2YS9sYW5nL1N5c3RlbTsAL1J1bm5pbmcgYWZ0ZXIgb3AyIHVzaW5nIHN0"
                  + "cnVjdHVyYWwgcmVkZWZpbml0aW9uAA1UZXN0MTk3OC5qYXZhAAlUcmFuc2Zvcm0AAVYAAlZMAANW"
                  + "TEwAC2FjY2Vzc0ZsYWdzAAZhcHBlbmQAB2RvUXVlcnkACGRvU2F5QnllAAdkb1NheUhpAARuYW1l"
                  + "AANvdXQAB3ByaW50bG4AA3J1bgAKc2F5QnllTmFtZQAFc2F5SGkACXNheUhpTmFtZQAIc2F5UXVl"
                  + "cnkAEnNvbWV0aGluZ0hhcHBlbmVkMQASc29tZXRoaW5nSGFwcGVuZWQyAAh0b1N0cmluZwAFdmFs"
                  + "dWUAdn5+RDh7ImNvbXBpbGF0aW9uLW1vZGUiOiJkZWJ1ZyIsIm1pbi1hcGkiOjEsInNoYS0xIjoi"
                  + "YTgzNTJmMjU0ODg1MzYyY2NkOGQ5MDlkMzUyOWM2MDA5NGRkODk2ZSIsInZlcnNpb24iOiIxLjYu"
                  + "MjAtZGV2In0AAgIBKhgBAgMCGgQIHxcWAwAIAAAJAQkBCQCIgAS4BAGAgATMBAEJ5AQBCbAFAQn8"
                  + "BQEJyAYBCYQHAQmsBwAAAAAAAAACAAAA8AYAAPYGAAA0BwAAAAAAAAAAAAAAAAAAEAAAAAAAAAAB"
                  + "AAAAAAAAAAEAAAAsAAAAcAAAAAIAAAALAAAAIAEAAAMAAAAFAAAATAEAAAQAAAAEAAAAiAEAAAUA"
                  + "AAAOAAAAqAEAAAYAAAABAAAAGAIAAAEgAAAIAAAAOAIAAAMgAAAIAAAA1AMAAAEQAAACAAAADAQA"
                  + "AAIgAAAsAAAAHAQAAAQgAAACAAAA8AYAAAAgAAABAAAA/wYAAAMQAAACAAAAMAcAAAYgAAABAAAA"
                  + "QAcAAAAQAAABAAAAUAcAAA==");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static void doTest() throws Exception {
    Transform.sayHi(
        () -> {
          System.out.println("Not doing anything here - op1");
        },
        () -> {
          System.out.println("Not doing anything here - op2");
        });
    Transform.sayHi(
        () -> {
          System.out.println("transforming calling function - non-structural");
          Redefinition.doCommonClassRedefinition(
              Transform.class, new byte[] {}, NON_STRUCTURAL_DEX_BYTES);
        },
        () -> {
          System.out.println("transforming calling function - structural");
          Redefinition.doCommonStructuralClassRedefinition(Transform.class, STRUCTURAL_DEX_BYTES);
        });
    Transform.sayHi(
        () -> {
          System.out.println("Not doing anything here - op1");
        },
        () -> {
          System.out.println("Not doing anything here - op2");
        });
  }
}
