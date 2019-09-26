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

class Second {
  public String getX() {
    return "X";
  }
  public String getY() {
    return "Y";
  }
  public String getZ() {
    return "Z";
  }
}

class SubC extends Super {
  int getValue() { return 24; }
}

class TestIntrinsicOatdump {
  Integer valueOf(int i) {
    // ProfileTestMultiDex is used also for testing oatdump for apps.
    // This is a regression test that oatdump can handle .data.bimg.rel.ro
    // entries pointing to the middle of the "boot image live objects" array.
    return Integer.valueOf(i);
  }
}

// Add a class with lots of methods so we can test profile guided compilation triggers.
// Start the name with 'Z' so that the class is added at the end of the dex file.
class ZLotsOfMethodsSecond {
  public void m1() {}
  public void m2() {}
  public void m3() {}
  public void m4() {}
  public void m5() {}
  public void m6() {}
  public void m7() {}
  public void m8() {}
  public void m9() {}
  public void m10() {}
  public void m11() {}
  public void m12() {}
  public void m13() {}
  public void m14() {}
  public void m15() {}
  public void m16() {}
  public void m17() {}
  public void m18() {}
  public void m19() {}
  public void m20() {}
  public void m21() {}
  public void m22() {}
  public void m23() {}
  public void m24() {}
  public void m25() {}
  public void m26() {}
  public void m27() {}
  public void m28() {}
  public void m29() {}
  public void m30() {}
  public void m31() {}
  public void m32() {}
  public void m33() {}
  public void m34() {}
  public void m35() {}
  public void m36() {}
  public void m37() {}
  public void m38() {}
  public void m39() {}
  public void m40() {}
  public void m41() {}
  public void m42() {}
  public void m43() {}
  public void m44() {}
  public void m45() {}
  public void m46() {}
  public void m47() {}
  public void m48() {}
  public void m49() {}
  public void m50() {}
  public void m51() {}
  public void m52() {}
  public void m53() {}
  public void m54() {}
  public void m55() {}
  public void m56() {}
  public void m57() {}
  public void m58() {}
  public void m59() {}
  public void m60() {}
  public void m61() {}
  public void m62() {}
  public void m63() {}
  public void m64() {}
  public void m65() {}
  public void m66() {}
  public void m67() {}
  public void m68() {}
  public void m69() {}
  public void m70() {}
  public void m71() {}
  public void m72() {}
  public void m73() {}
  public void m74() {}
  public void m75() {}
  public void m76() {}
  public void m77() {}
  public void m78() {}
  public void m79() {}
  public void m80() {}
  public void m81() {}
  public void m82() {}
  public void m83() {}
  public void m84() {}
  public void m85() {}
  public void m86() {}
  public void m87() {}
  public void m88() {}
  public void m89() {}
  public void m90() {}
  public void m91() {}
  public void m92() {}
  public void m93() {}
  public void m94() {}
  public void m95() {}
  public void m96() {}
  public void m97() {}
  public void m98() {}
  public void m99() {}
  public void m100() {}
  public void m101() {}
  public void m102() {}
  public void m103() {}
  public void m104() {}
  public void m105() {}
  public void m106() {}
  public void m107() {}
  public void m108() {}
  public void m109() {}
  public void m110() {}
  public void m111() {}
  public void m112() {}
  public void m113() {}
  public void m114() {}
  public void m115() {}
  public void m116() {}
  public void m117() {}
  public void m118() {}
  public void m119() {}
  public void m120() {}
  public void m121() {}
  public void m122() {}
  public void m123() {}
  public void m124() {}
  public void m125() {}
  public void m126() {}
  public void m127() {}
  public void m128() {}
  public void m129() {}
  public void m130() {}
  public void m131() {}
  public void m132() {}
  public void m133() {}
  public void m134() {}
  public void m135() {}
  public void m136() {}
  public void m137() {}
  public void m138() {}
  public void m139() {}
  public void m140() {}
  public void m141() {}
  public void m142() {}
  public void m143() {}
  public void m144() {}
  public void m145() {}
  public void m146() {}
  public void m147() {}
  public void m148() {}
  public void m149() {}
  public void m150() {}
}
