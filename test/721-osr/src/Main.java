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

import java.util.ArrayList;
import java.util.List;

public class Main {
    private enum TestType {
        ONE,
        TWO
    }

    private static TestType type = TestType.ONE;

    public static void main(String[] args) {
        float testFloat;
        switch (type) {
            case ONE: testFloat = 1000.0f; break;
            default: testFloat = 5f; break;
        }

        // Loop enough to potentially trigger OSR.
        List<Integer> dummyObjects = new ArrayList<Integer>(200_000);
        for (int i = 0; i < 200_000; i++) {
            dummyObjects.add(1024);
        }

        if (testFloat != 1000.0f) {
          throw new Error("Expected 1000.0f, got " + testFloat);
        }
    }
}
