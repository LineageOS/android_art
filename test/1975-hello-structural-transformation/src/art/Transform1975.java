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

public class Transform1975 {

  static {
  }

  public static Class<?> CUR_CLASS = Transform1975.class;

  /* Dex file for:
   * // NB The name NEW_STRING ensures the offset for the REDEFINED_DEX_BYTES field is different.
   * package art;
   * public class Transform1975 {
   *  static {}
   *  public static Class<?> CUR_CLASS;
   *  public static byte[] REDEFINED_DEX_BYTES;
   *  public static String NEW_STRING;
   *  public static void doSomething() {
   *    System.out.println("Doing something");
   *    new_string = "I did something!";
   *  }
   *  public static void readFields() {
   *    System.out.println("NEW VALUE CUR_CLASS: " + CUR_CLASS);
   *    System.out.println("NEW VALUE REDEFINED_DEX_BYTES: " + Base64.getEncoder().encodeToString(REDEFINED_DEX_BYTES));
   *    System.out.println("NEW VALUE NEW_STRING: " + NEW_STRING);
   *  }
   * }
   */
  public static byte[] REDEFINED_DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQDNGFvYlmyIt+u4bnNv+OyNVekBxlrJi6EgBwAAcAAAAHhWNBIAAAAAAAAAAFwGAAAm"
                  + "AAAAcAAAAAwAAAAIAQAABwAAADgBAAAEAAAAjAEAAAwAAACsAQAAAQAAAAwCAAD0BAAALAIAAGYD"
                  + "AABrAwAAdQMAAH0DAACIAwAAmQMAAKsDAACuAwAAsgMAAMcDAADmAwAA/QMAABAEAAAjBAAANwQA"
                  + "AEsEAABmBAAAegQAAJYEAACqBAAAwQQAANkEAAD6BAAABgUAABsFAAAvBQAAMgUAADYFAAA6BQAA"
                  + "QgUAAE8FAABfBQAAawUAAHAFAAB5BQAAhQUAAI8FAACWBQAACAAAAAkAAAAKAAAACwAAAA0AAAAO"
                  + "AAAADwAAABAAAAARAAAAEgAAABkAAAAbAAAABgAAAAUAAAAAAAAABwAAAAUAAABQAwAABwAAAAYA"
                  + "AABYAwAABwAAAAYAAABgAwAABgAAAAgAAAAAAAAAGQAAAAoAAAAAAAAAGgAAAAoAAABgAwAAAAAD"
                  + "AAMAAAAAAAUAFgAAAAAACwAXAAAABwACACAAAAAAAAUAAQAAAAAABQACAAAAAAAFAB0AAAAAAAUA"
                  + "IgAAAAIABgAhAAAABAAFAAIAAAAGAAUAAgAAAAYAAgAcAAAABgADABwAAAAGAAAAIwAAAAgAAQAe"
                  + "AAAACQAEAB8AAAAAAAAAAQAAAAQAAAAAAAAAGAAAAEQGAAAYBgAAAAAAAAAAAAAAAAAAMgMAAAEA"
                  + "AAAOAAAAAQABAAEAAAA2AwAABAAAAHAQBQAAAA4AAgAAAAIAAAA6AwAADAAAAGIAAwAaAQQAbiAE"
                  + "ABAAGgAFAGkAAQAOAAQAAAACAAAAQAMAAFEAAABiAAMAYgEAACICBgBwEAYAAgAaAxMAbiAIADIA"
                  + "biAHABIAbhAJAAIADAFuIAQAEABiAAMAcQALAAAADAFiAgIAbiAKACEADAEiAgYAcBAGAAIAGgMV"
                  + "AG4gCAAyAG4gCAASAG4QCQACAAwBbiAEABAAYgADAGIBAQAiAgYAcBAGAAIAGgMUAG4gCAAyAG4g"
                  + "CAASAG4QCQACAAwBbiAEABAADgAEAA4AAwAOAAkADnhLAA0ADgEYDwEgDwEYDwAAAAABAAAACwAA"
                  + "AAEAAAAEAAAAAQAAAAUAAyo+OwAIPGNsaW5pdD4ABjxpbml0PgAJQ1VSX0NMQVNTAA9Eb2luZyBz"
                  + "b21ldGhpbmcAEEkgZGlkIHNvbWV0aGluZyEAAUwAAkxMABNMYXJ0L1RyYW5zZm9ybTE5NzU7AB1M"
                  + "ZGFsdmlrL2Fubm90YXRpb24vU2lnbmF0dXJlOwAVTGphdmEvaW8vUHJpbnRTdHJlYW07ABFMamF2"
                  + "YS9sYW5nL0NsYXNzOwARTGphdmEvbGFuZy9DbGFzczwAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGph"
                  + "dmEvbGFuZy9TdHJpbmc7ABlMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7ABJMamF2YS9sYW5nL1N5"
                  + "c3RlbTsAGkxqYXZhL3V0aWwvQmFzZTY0JEVuY29kZXI7ABJMamF2YS91dGlsL0Jhc2U2NDsAFU5F"
                  + "VyBWQUxVRSBDVVJfQ0xBU1M6IAAWTkVXIFZBTFVFIE5FV19TVFJJTkc6IAAfTkVXIFZBTFVFIFJF"
                  + "REVGSU5FRF9ERVhfQllURVM6IAAKTkVXX1NUUklORwATUkVERUZJTkVEX0RFWF9CWVRFUwASVHJh"
                  + "bnNmb3JtMTk3NS5qYXZhAAFWAAJWTAACW0IABmFwcGVuZAALZG9Tb21ldGhpbmcADmVuY29kZVRv"
                  + "U3RyaW5nAApnZXRFbmNvZGVyAANvdXQAB3ByaW50bG4ACnJlYWRGaWVsZHMACHRvU3RyaW5nAAV2"
                  + "YWx1ZQB2fn5EOHsiY29tcGlsYXRpb24tbW9kZSI6ImRlYnVnIiwibWluLWFwaSI6MSwic2hhLTEi"
                  + "OiJhODM1MmYyNTQ4ODUzNjJjY2Q4ZDkwOWQzNTI5YzYwMDk0ZGQ4OTZlIiwidmVyc2lvbiI6IjEu"
                  + "Ni4yMC1kZXYifQACAQEkHAIXDBcAAwAEAAAJAQkBCQCIgASsBAGBgATABAEJ2AQBCYAFAAAAAAAA"
                  + "AQAAAA4GAAA4BgAAAQAAAAAAAAAAAAAAAAAAADwGAAAQAAAAAAAAAAEAAAAAAAAAAQAAACYAAABw"
                  + "AAAAAgAAAAwAAAAIAQAAAwAAAAcAAAA4AQAABAAAAAQAAACMAQAABQAAAAwAAACsAQAABgAAAAEA"
                  + "AAAMAgAAASAAAAQAAAAsAgAAAyAAAAQAAAAyAwAAARAAAAMAAABQAwAAAiAAACYAAABmAwAABCAA"
                  + "AAEAAAAOBgAAACAAAAEAAAAYBgAAAxAAAAIAAAA4BgAABiAAAAEAAABEBgAAABAAAAEAAABcBgAA");

  public static void doSomething() {
    System.out.println("Not doing anything");
  }

  public static void readFields() {
    System.out.println("ORIGINAL VALUE CUR_CLASS: " + CUR_CLASS);
    System.out.println(
        "ORIGINAL VALUE REDEFINED_DEX_BYTES: "
            + Base64.getEncoder().encodeToString(REDEFINED_DEX_BYTES));
  }
}
