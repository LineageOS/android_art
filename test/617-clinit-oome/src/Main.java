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

public class Main {
  private static int exhaustJavaHeap(Object[] data, int index, int size) {
    Runtime.getRuntime().gc();
    while (size > 0) {
        try {
            data[index] = new byte[size];
            index++;
        } catch (OutOfMemoryError e) {
            size /= 2;
        }
    }
    return index;
  }

  public static void main(String[] args) {
    Class klass = Other.class;
    Object[] data = new Object[100000];
    try {
        System.out.println("Filling heap");

        // Make sure that there is no reclaimable memory in the heap. Otherwise we may throw
        // OOME to prevent GC thrashing, even if later allocations may succeed.
        Runtime.getRuntime().gc();
        System.runFinalization();
        // NOTE: There is a GC invocation in the exhaustJavaHeap(). So we don't need one here.

        int index = 0;
        int initial_size = 256 * 1024 * 1024;
        // Repeat to ensure there is no space left on the heap.
        index = exhaustJavaHeap(data, index, initial_size);
        index = exhaustJavaHeap(data, index, /*size*/ 4);
        index = exhaustJavaHeap(data, index, /*size*/ 4);

        // Initialize now that the heap is full.
        Other.print();
    } catch (OutOfMemoryError e) {
    } catch (Exception e) {
        System.out.println(e);
    }
  }
}
