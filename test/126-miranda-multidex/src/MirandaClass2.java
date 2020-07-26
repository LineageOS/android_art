/*
 * Copyright (C) 2006 The Android Open Source Project
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

class MirandaClass2 extends MirandaAbstract {
    public boolean inInterface() {
        return true;
    }

    public int inInterface2() {
        return 28;
    }

    // Better not hit any of these...
    public void inInterfaceUnused1() {
        System.out.println("inInterfaceUnused1");
    }
    public void inInterfaceUnused2() {
        System.out.println("inInterfaceUnused2");
    }
    public void inInterfaceUnused3() {
        System.out.println("inInterfaceUnused3");
    }
    public void inInterfaceUnused4() {
        System.out.println("inInterfaceUnused4");
    }
    public void inInterfaceUnused5() {
        System.out.println("inInterfaceUnused5");
    }
}
