/*
 * Copyright (C) 2017 The Android Open Source Project
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

import art.*;
import java.util.*;
import java.lang.invoke.*;
import java.io.*;
import java.util.zip.*;

public class Main {
  public static final String TEST_NAME = "2000-virtual-list-structural";
  public static final boolean PRINT_COUNT = false;
  public static MethodHandles.Lookup lookup = MethodHandles.publicLookup();
  public static MethodHandle getcnt;
  public static MethodHandle get_total_cnt;
  public static void GetHandles() throws Throwable {
    getcnt = lookup.findGetter(AbstractCollection.class, "cnt", Integer.TYPE);
    get_total_cnt = lookup.findStaticGetter(AbstractCollection.class, "TOTAL_COUNT", Integer.TYPE);
  }

  public static byte[] GetDexBytes() throws Throwable {
    String jar_loc = System.getenv("DEX_LOCATION") + "/" + TEST_NAME + "-ex.jar";
    try (ZipFile zip = new ZipFile(new File(jar_loc))) {
      ZipEntry entry = zip.getEntry("classes.dex");
      try (InputStream is = zip.getInputStream(entry)) {
        byte[] res = new byte[(int)entry.getSize()];
        is.read(res);
        return res;
      }
    }
  }
  public static void PrintListAndData(AbstractCollection<String> c) throws Throwable {
    if (PRINT_COUNT) {
      System.out.println("List is: " + c + " count = " + getcnt.invoke(c) + " TOTAL_COUNT = " + get_total_cnt.invoke());
    } else {
      System.out.println("List is: " + c);
    }
  }
  public static void main(String[] args) throws Throwable {
    AbstractCollection<String> l1 = (AbstractCollection<String>)Arrays.asList("a", "b", "c", "d");
    AbstractCollection<String> l2 = new ArrayList<>();
    l2.add("1");
    l2.add("2");
    l2.add("3");
    l2.add("4");
    Redefinition.doCommonStructuralClassRedefinition(AbstractCollection.class, GetDexBytes());
    GetHandles();
    AbstractCollection<String> l3 = new HashSet<>(l2);
    AbstractCollection<String> l4 = new LinkedList<>(l1);
    PrintListAndData(l1);
    PrintListAndData(l2);
    for (int i = 0; i < 1000; i++) {
      l2.add("xyz: " + i);
    }
    PrintListAndData(l2);
    PrintListAndData(l3);
    PrintListAndData(l4);
    CheckLE(getcnt.invoke(l1), get_total_cnt.invoke());
    CheckLE(getcnt.invoke(l2), get_total_cnt.invoke());
    CheckLE(getcnt.invoke(l3), get_total_cnt.invoke());
    CheckLE(getcnt.invoke(l4), get_total_cnt.invoke());
    CheckEQ(getcnt.invoke(l1), 0);
    CheckLE(getcnt.invoke(l2), 0);
    CheckLE(getcnt.invoke(l1), getcnt.invoke(l2));
    CheckLE(getcnt.invoke(l1), getcnt.invoke(l3));
    CheckLE(getcnt.invoke(l1), getcnt.invoke(l4));
    CheckLE(getcnt.invoke(l2), getcnt.invoke(l3));
    CheckLE(getcnt.invoke(l2), getcnt.invoke(l4));
    CheckLE(getcnt.invoke(l3), getcnt.invoke(l4));
  }
  public static void CheckEQ(Object a, int b) {
    CheckEQ(((Integer)a).intValue(), b);
  }
  public static void CheckLE(Object a, Object b) {
    CheckLE(((Integer)a).intValue(), ((Integer)b).intValue());
  }
  public static void CheckEQ(int a, int b) {
    if (a != b) {
      throw new Error(a + " is not equal to " + b);
    }
  }
  public static void CheckLE(int a, int b) {
    if (!(a <= b)) {
      throw new Error(a + " is not less than or equal to " + b);
    }
  }
}
