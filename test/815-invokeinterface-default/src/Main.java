/*
 * Copyright (C) 2021 The Android Open Source Project
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

import java.lang.reflect.Method;

// An interface with enough methods to trigger a conflict.
interface Itf {
  public void method0a();
  public void method0b();
  public void method0c();
  public void method0d();
  public void method0e();
  public void method0f();
  public void method0g();
  public void method0h();
  public void method0i();
  public void method0j();
  public void method0k();
  public void method0l();
  public void method0m();
  public void method0n();
  public void method0o();
  public void method0p();
  public void method0q();
  public void method0r();
  public void method0s();
  public void method0t();
  public void method0u();
  public void method0v();
  public void method0w();
  public void method0x();
  public void method0y();
  public void method0z();
  public void method1a();
  public void method1b();
  public void method1c();
  public void method1d();
  public void method1e();
  public void method1f();
  public void method1g();
  public void method1h();
  public void method1i();
  public void method1j();
  public void method1k();
  public void method1l();
  public void method1m();
  public void method1n();
  public void method1o();
  public void method1p();
  public void method1q();
  public void method1r();
  public void method1s();
  public void method1t();
  public void method1u();
  public void method1v();
  public void method1w();
  public void method1x();
  public void method1y();
  public void method1z();
  public void method2a();
  public void method2b();
  public void method2c();
  public void method2d();
  public void method2e();
  public void method2f();
  public void method2g();
  public void method2h();
  public void method2i();
  public void method2j();
  public void method2k();
  public void method2l();
  public void method2m();
  public void method2n();
  public void method2o();
  public void method2p();
  public void method2q();
  public void method2r();
  public void method2s();
  public void method2t();
  public void method2u();
  public void method2v();
  public void method2w();
  public void method2x();
  public void method2y();
  public void method2z();

  public default void $noinline$defaultRecursiveMethod(boolean callRecursive) {
    if (callRecursive) {
      $noinline$defaultRecursiveMethod(false);
    }
  }
}

public class Main implements Itf {
  public static void main(String[] args) throws Exception {
    Main main = new Main();
    main.$noinline$defaultRecursiveMethod(true);
  }

  public void method0a() {}
  public void method0b() {}
  public void method0c() {}
  public void method0d() {}
  public void method0e() {}
  public void method0f() {}
  public void method0g() {}
  public void method0h() {}
  public void method0i() {}
  public void method0j() {}
  public void method0k() {}
  public void method0l() {}
  public void method0m() {}
  public void method0n() {}
  public void method0o() {}
  public void method0p() {}
  public void method0q() {}
  public void method0r() {}
  public void method0s() {}
  public void method0t() {}
  public void method0u() {}
  public void method0v() {}
  public void method0w() {}
  public void method0x() {}
  public void method0y() {}
  public void method0z() {}
  public void method1a() {}
  public void method1b() {}
  public void method1c() {}
  public void method1d() {}
  public void method1e() {}
  public void method1f() {}
  public void method1g() {}
  public void method1h() {}
  public void method1i() {}
  public void method1j() {}
  public void method1k() {}
  public void method1l() {}
  public void method1m() {}
  public void method1n() {}
  public void method1o() {}
  public void method1p() {}
  public void method1q() {}
  public void method1r() {}
  public void method1s() {}
  public void method1t() {}
  public void method1u() {}
  public void method1v() {}
  public void method1w() {}
  public void method1x() {}
  public void method1y() {}
  public void method1z() {}
  public void method2a() {}
  public void method2b() {}
  public void method2c() {}
  public void method2d() {}
  public void method2e() {}
  public void method2f() {}
  public void method2g() {}
  public void method2h() {}
  public void method2i() {}
  public void method2j() {}
  public void method2k() {}
  public void method2l() {}
  public void method2m() {}
  public void method2n() {}
  public void method2o() {}
  public void method2p() {}
  public void method2q() {}
  public void method2r() {}
  public void method2s() {}
  public void method2t() {}
  public void method2u() {}
  public void method2v() {}
  public void method2w() {}
  public void method2x() {}
  public void method2y() {}
  public void method2z() {}
}
