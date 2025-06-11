/*
 * Copyright (C) 2025 The Android Open Source Project
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

import java.io.ByteArrayInputStream;
import java.io.InvalidClassException;
import java.io.ObjectInputStream;

public class Main {

  public static void main(String[] args) throws Exception {
    deserializeHexToConcurrentHashMap();
  }

  public static byte[] hexStringToByteArray(String hexString) {
    if (hexString == null || hexString.isEmpty()) {
      return new byte[0];
    }
    if (hexString.length() % 2 != 0) {
      throw new IllegalArgumentException("Hex string must have an even number of characters.");
    }
    int len = hexString.length();
    byte[] data = new byte[len / 2];
    for (int i = 0; i < len; i += 2) {
      int highNibble = Character.digit(hexString.charAt(i), 16);
      int lowNibble = Character.digit(hexString.charAt(i + 1), 16);
      if (highNibble == -1 || lowNibble == -1) {
        throw new IllegalArgumentException(
            "Invalid hex character in string: " + hexString.charAt(i) + hexString.charAt(i + 1));
      }
      data[i / 2] = (byte) ((highNibble << 4) + lowNibble);
    }
    return data;
  }

  public static void deserializeHexToConcurrentHashMap() throws Exception {
    byte[] bytes = hexStringToByteArray("ACED0005737200266A6176612E7574696C2E636F6E63757272656E742E436F6E63757272656E74486173684D61706499DE129D87293D0300007870737200146A6176612E746578742E44617465466F726D6174642CA1E4C22615FC0200007870737200146A6176612E746578742E44617465466F726D6174642CA1E4C22615FC020000787070707878000000");
    ByteArrayInputStream bis = new ByteArrayInputStream(bytes);
    ObjectInputStream ois = new ObjectInputStream(bis);
    try {
      Object deserializedObject = ois.readObject();
      throw new Error("Expected InvalidClassException");
    } catch (InvalidClassException e) {
      // expected
      if (!(e.getCause() instanceof InstantiationException)) {
        throw new Error("Expected InstantiationException");
      }
    }
  }
}
