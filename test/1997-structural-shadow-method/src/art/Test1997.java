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

public class Test1997 {

  public static class SuperTransform {
    // We will be shadowing this function.
    public static void sayHi() {
      System.out.println("Hello!");
    }
  }

  // The class we will be transforming.
  public static class Transform extends SuperTransform {
    public static void sayHiTwice() {
      Transform.sayHi();
      Transform.sayHi();
    }
  }

  // public static class Transform extends SuperTransform {
  //   public static void sayHiTwice() {
  //     Transform.sayHi();
  //     Transform.sayHi();
  //   }
  //   public static void sayHi() {
  //     System.out.println("Hello World!");
  //   }
  // }
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
              "ZGV4CjAzNQA9wdy7Lgbrv+sD+wixborREr0maZCK5yqABAAAcAAAAHhWNBIAAAAAAAAAALwDAAAW"
                  + "AAAAcAAAAAkAAADIAAAAAgAAAOwAAAABAAAABAEAAAUAAAAMAQAAAQAAADQBAAAsAwAAVAEAAMIB"
                  + "AADKAQAA2AEAAPcBAAARAgAAIQIAAEUCAABlAgAAfAIAAJACAACkAgAAswIAAL4CAADBAgAAxQIA"
                  + "ANICAADYAgAA3QIAAOYCAADtAgAA+QIAAAADAAACAAAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAA"
                  + "CQAAAAwAAAAMAAAACAAAAAAAAAANAAAACAAAALwBAAAHAAUAEAAAAAAAAAAAAAAAAQAAAAAAAAAB"
                  + "AAAAEgAAAAEAAAATAAAABQABABEAAAABAAAAAQAAAAAAAAAAAAAACgAAAKwDAACHAwAAAAAAAAEA"
                  + "AQABAAAAqgEAAAQAAABwEAAAAAAOAAIAAAACAAAArgEAAAgAAABiAAAAGgEBAG4gBAAQAA4AAAAA"
                  + "AAAAAACzAQAABwAAAHEAAgAAAHEAAgAAAA4ADwAOABUADngAEQAOPDwAAAAAAQAAAAYABjxpbml0"
                  + "PgAMSGVsbG8gV29ybGQhAB1MYXJ0L1Rlc3QxOTk3JFN1cGVyVHJhbnNmb3JtOwAYTGFydC9UZXN0"
                  + "MTk5NyRUcmFuc2Zvcm07AA5MYXJ0L1Rlc3QxOTk3OwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xv"
                  + "c2luZ0NsYXNzOwAeTGRhbHZpay9hbm5vdGF0aW9uL0lubmVyQ2xhc3M7ABVMamF2YS9pby9Qcmlu"
                  + "dFN0cmVhbTsAEkxqYXZhL2xhbmcvU3RyaW5nOwASTGphdmEvbGFuZy9TeXN0ZW07AA1UZXN0MTk5"
                  + "Ny5qYXZhAAlUcmFuc2Zvcm0AAVYAAlZMAAthY2Nlc3NGbGFncwAEbmFtZQADb3V0AAdwcmludGxu"
                  + "AAVzYXlIaQAKc2F5SGlUd2ljZQAFdmFsdWUAdn5+RDh7ImNvbXBpbGF0aW9uLW1vZGUiOiJkZWJ1"
                  + "ZyIsIm1pbi1hcGkiOjEsInNoYS0xIjoiNjBkYTRkNjdiMzgxYzQyNDY3NzU3YzQ5ZmI2ZTU1NzU2"
                  + "ZDg4YTJmMyIsInZlcnNpb24iOiIxLjcuMTItZGV2In0AAgMBFBgCAgQCDgQJDxcLAAADAAGBgATU"
                  + "AgEJ7AIBCYwDAAAAAAAAAAIAAAB4AwAAfgMAAKADAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAEAAAAA"
                  + "AAAAAQAAABYAAABwAAAAAgAAAAkAAADIAAAAAwAAAAIAAADsAAAABAAAAAEAAAAEAQAABQAAAAUA"
                  + "AAAMAQAABgAAAAEAAAA0AQAAASAAAAMAAABUAQAAAyAAAAMAAACqAQAAARAAAAEAAAC8AQAAAiAA"
                  + "ABYAAADCAQAABCAAAAIAAAB4AwAAACAAAAEAAACHAwAAAxAAAAIAAACcAwAABiAAAAEAAACsAwAA"
                  + "ABAAAAEAAAC8AwAA");

  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static void doTest() throws Exception {
    Transform.sayHiTwice();
    Transform.sayHi();
    Redefinition.doCommonStructuralClassRedefinition(Transform.class, DEX_BYTES);
    Transform.sayHiTwice();
    Transform.sayHi();
  }
}
