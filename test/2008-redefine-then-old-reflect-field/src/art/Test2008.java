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

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Base64;
import java.util.concurrent.CountDownLatch;
import java.util.function.Supplier;
import java.util.concurrent.atomic.*;
import java.lang.reflect.*;

public class Test2008 {
  public static class Transform {
    public Transform() { myField = "bar"; }
    public Object myField;
  }

  /**
   * base64 encoded class/dex file for
   * public static class Transform {
   *   public Transform() { myField = "foo"; };
   *   public Object myField;
   * }
   */
  private static final byte[] DEX_BYTES =
      Base64.getDecoder()
          .decode(
"ZGV4CjAzNQC9mLO3NCcl4Iqwlj+DV0clWONvLK5zDAqAAwAAcAAAAHhWNBIAAAAAAAAAAMgCAAAP" +
"AAAAcAAAAAYAAACsAAAAAQAAAMQAAAABAAAA0AAAAAIAAADYAAAAAQAAAOgAAAB4AgAACAEAACwB" +
"AAA0AQAATgEAAF4BAACCAQAAogEAALYBAADFAQAA0AEAANMBAADgAQAA5QEAAO4BAAD0AQAA+wEA" +
"AAEAAAACAAAAAwAAAAQAAAAFAAAACAAAAAgAAAAFAAAAAAAAAAAABAALAAAAAAAAAAAAAAAEAAAA" +
"AAAAAAAAAAABAAAABAAAAAAAAAAGAAAAuAIAAJkCAAAAAAAAAgABAAEAAAAoAQAACAAAAHAQAQAB" +
"ABoACgBbEAAADgAFAA4ABjxpbml0PgAYTGFydC9UZXN0MjAwOCRUcmFuc2Zvcm07AA5MYXJ0L1Rl" +
"c3QyMDA4OwAiTGRhbHZpay9hbm5vdGF0aW9uL0VuY2xvc2luZ0NsYXNzOwAeTGRhbHZpay9hbm5v" +
"dGF0aW9uL0lubmVyQ2xhc3M7ABJMamF2YS9sYW5nL09iamVjdDsADVRlc3QyMDA4LmphdmEACVRy" +
"YW5zZm9ybQABVgALYWNjZXNzRmxhZ3MAA2ZvbwAHbXlGaWVsZAAEbmFtZQAFdmFsdWUAjAF+fkQ4" +
"eyJjb21waWxhdGlvbi1tb2RlIjoiZGVidWciLCJoYXMtY2hlY2tzdW1zIjpmYWxzZSwibWluLWFw" +
"aSI6MSwic2hhLTEiOiI2NjA0MGE0MGQzY2JmNDA1MDU0NzQ4YmY1YTllOWYyZjNmZThhMzRiIiwi" +
"dmVyc2lvbiI6IjIuMC4xMi1kZXYifQACAgENGAECAwIJBAkMFwcAAQEAAAEAgYAEiAIAAAAAAAAA" +
"AgAAAIoCAACQAgAArAIAAAAAAAAAAAAAAAAAAA8AAAAAAAAAAQAAAAAAAAABAAAADwAAAHAAAAAC" +
"AAAABgAAAKwAAAADAAAAAQAAAMQAAAAEAAAAAQAAANAAAAAFAAAAAgAAANgAAAAGAAAAAQAAAOgA" +
"AAABIAAAAQAAAAgBAAADIAAAAQAAACgBAAACIAAADwAAACwBAAAEIAAAAgAAAIoCAAAAIAAAAQAA" +
"AJkCAAADEAAAAgAAAKgCAAAGIAAAAQAAALgCAAAAEAAAAQAAAMgCAAA=");
  private static final byte[] CLASS_BYTES =
      Base64.getDecoder()
          .decode(
"yv66vgAAADQAFwoABQAOCAAPCQAEABAHABIHABUBAAdteUZpZWxkAQASTGphdmEvbGFuZy9PYmpl" +
"Y3Q7AQAGPGluaXQ+AQADKClWAQAEQ29kZQEAD0xpbmVOdW1iZXJUYWJsZQEAClNvdXJjZUZpbGUB" +
"AA1UZXN0MjAwOC5qYXZhDAAIAAkBAANmb28MAAYABwcAFgEAFmFydC9UZXN0MjAwOCRUcmFuc2Zv" +
"cm0BAAlUcmFuc2Zvcm0BAAxJbm5lckNsYXNzZXMBABBqYXZhL2xhbmcvT2JqZWN0AQAMYXJ0L1Rl" +
"c3QyMDA4ACEABAAFAAAAAQABAAYABwAAAAEAAQAIAAkAAQAKAAAAIwACAAEAAAALKrcAASoSArUA" +
"A7EAAAABAAsAAAAGAAEAAAAFAAIADAAAAAIADQAUAAAACgABAAQAEQATAAk=");


  public static void run() throws Exception {
    Redefinition.setTestConfiguration(Redefinition.Config.COMMON_REDEFINE);
    doTest();
  }

  public static void doTest() throws Exception {
    Transform t = new Transform();
    Field f = Transform.class.getDeclaredField("myField");
    System.out.println("PreTransform Field " + f + " = \"" + f.get(t) + "\"");
    Redefinition.doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
    System.out.println("PostTransform Field " + f + " = \"" + f.get(t) + "\"");
  }
}
