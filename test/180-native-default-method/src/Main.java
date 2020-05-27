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

public class Main {
  public static void main(String args[]) {
    try {
      // Regression test for default native methods that should cause ClassFormatException
      // if they pass the dex file verification, i.e. for old dex file versions.
      // We previously did not handle this case properly and failed a DCHECK() for
      // a non-interface class creating a copied method that was native. b/157170505
      Class.forName("TestClass");
      throw new Error("UNREACHABLE");
    } catch (ClassFormatError expected) {
      System.out.println("passed");
    } catch (Throwable unexpected) {
      unexpected.printStackTrace();
    }
  }
}
