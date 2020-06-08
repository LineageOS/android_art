/*
 * Copyright 2019 The Android Open Source Project
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

#include "jni.h"

namespace art {

static inline bool VerifyManyParameters(
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  return
      (i1 == 11) && (l1 == 12) && (f1 == 13.0) && (d1 == 14.0) &&
      (i2 == 21) && (l2 == 22) && (f2 == 23.0) && (d2 == 24.0) &&
      (i3 == 31) && (l3 == 32) && (f3 == 33.0) && (d3 == 34.0) &&
      (i4 == 41) && (l4 == 42) && (f4 == 43.0) && (d4 == 44.0) &&
      (i5 == 51) && (l5 == 52) && (f5 == 53.0) && (d5 == 54.0) &&
      (i6 == 61) && (l6 == 62) && (f6 == 63.0) && (d6 == 64.0) &&
      (i7 == 71) && (l7 == 72) && (f7 == 73.0) && (d7 == 74.0) &&
      (i8 == 81) && (l8 == 82) && (f8 == 83.0) && (d8 == 84.0);
}

extern "C" JNIEXPORT jint JNICALL Java_Test_nativeMethodVoid(JNIEnv*, jclass) {
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_Test_nativeMethod(JNIEnv*, jclass, jint i) {
  return i;
}

extern "C" JNIEXPORT jint JNICALL Java_Test_nativeMethodWithManyParameters(
    JNIEnv*, jclass,
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  bool ok = VerifyManyParameters(
      i1, l1, f1, d1,
      i2, l2, f2, d2,
      i3, l3, f3, d3,
      i4, l4, f4, d4,
      i5, l5, f5, d5,
      i6, l6, f6, d6,
      i7, l7, f7, d7,
      i8, l8, f8, d8);
  return ok ? 42 : -1;
}

extern "C" JNIEXPORT jint JNICALL Java_TestFast_nativeMethodVoid(JNIEnv*, jclass) {
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_TestFast_nativeMethod(JNIEnv*, jclass, jint i) {
  return i;
}

extern "C" JNIEXPORT jint JNICALL Java_TestFast_nativeMethodWithManyParameters(
    JNIEnv*, jclass,
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  bool ok = VerifyManyParameters(
      i1, l1, f1, d1,
      i2, l2, f2, d2,
      i3, l3, f3, d3,
      i4, l4, f4, d4,
      i5, l5, f5, d5,
      i6, l6, f6, d6,
      i7, l7, f7, d7,
      i8, l8, f8, d8);
  return ok ? 42 : -1;
}

extern "C" JNIEXPORT jint JNICALL Java_TestCritical_nativeMethodVoid() {
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_TestCritical_nativeMethod(jint i) {
  return i;
}

extern "C" JNIEXPORT jint JNICALL Java_TestCritical_nativeMethodWithManyParameters(
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  bool ok = VerifyManyParameters(
      i1, l1, f1, d1,
      i2, l2, f2, d2,
      i3, l3, f3, d3,
      i4, l4, f4, d4,
      i5, l5, f5, d5,
      i6, l6, f6, d6,
      i7, l7, f7, d7,
      i8, l8, f8, d8);
  return ok ? 42 : -1;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalSignatures_nativeILFFFFD(
    jint i,
    jlong l,
    jfloat f1,
    jfloat f2,
    jfloat f3,
    jfloat f4,
    jdouble d) {
  if (i != 1) return -1;
  if (l != INT64_C(0xf00000002)) return -2;
  if (f1 != 3.0f) return -3;
  if (f2 != 4.0f) return -4;
  if (f3 != 5.0f) return -5;
  if (f4 != 6.0f) return -6;
  if (d != 7.0) return -7;
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalSignatures_nativeLIFFFFD(
    jlong l,
    jint i,
    jfloat f1,
    jfloat f2,
    jfloat f3,
    jfloat f4,
    jdouble d) {
  if (l != INT64_C(0xf00000007)) return -1;
  if (i != 6) return -2;
  if (f1 != 5.0f) return -3;
  if (f2 != 4.0f) return -4;
  if (f3 != 3.0f) return -5;
  if (f4 != 2.0f) return -6;
  if (d != 1.0) return -7;
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalSignatures_nativeFLIFFFD(
    jfloat f1,
    jlong l,
    jint i,
    jfloat f2,
    jfloat f3,
    jfloat f4,
    jdouble d) {
  if (f1 != 1.0f) return -3;
  if (l != INT64_C(0xf00000002)) return -1;
  if (i != 3) return -2;
  if (f2 != 4.0f) return -4;
  if (f3 != 5.0f) return -5;
  if (f4 != 6.0f) return -6;
  if (d != 7.0) return -7;
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalSignatures_nativeDDIIIIII(
    jdouble d1,
    jdouble d2,
    jint i1,
    jint i2,
    jint i3,
    jint i4,
    jint i5,
    jint i6) {
  if (d1 != 8.0) return -1;
  if (d2 != 7.0) return -2;
  if (i1 != 6) return -3;
  if (i2 != 5) return -4;
  if (i3 != 4) return -5;
  if (i4 != 3) return -6;
  if (i5 != 2) return -7;
  if (i6 != 1) return -8;
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalSignatures_nativeDFFILIII(
    jdouble d,
    jfloat f1,
    jfloat f2,
    jint i1,
    jlong l,
    jint i2,
    jint i3,
    jint i4) {
  if (d != 1.0) return -1;
  if (f1 != 2.0f) return -2;
  if (f2 != 3.0f) return -3;
  if (i1 != 4) return -4;
  if (l != INT64_C(0xf00000005)) return -5;
  if (i2 != 6) return -6;
  if (i3 != 7) return -7;
  if (i4 != 8) return -8;
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalSignatures_nativeDDFILIII(
    jdouble d1,
    jdouble d2,
    jfloat f,
    jint i1,
    jlong l,
    jint i2,
    jint i3,
    jint i4) {
  if (d1 != 8.0) return -1;
  if (d2 != 7.0) return -2;
  if (f != 6.0f) return -3;
  if (i1 != 5) return -4;
  if (l != INT64_C(0xf00000004)) return -5;
  if (i2 != 3) return -6;
  if (i3 != 2) return -7;
  if (i4 != 1) return -8;
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalSignatures_nativeDDIFII(
    jdouble d1,
    jdouble d2,
    jint i1,
    jfloat f,
    jint i2,
    jint i3) {
  if (d1 != 1.0) return -1;
  if (d2 != 2.0) return -2;
  if (i1 != 3) return -3;
  if (f != 4.0f) return -4;
  if (i2 != 5) return -5;
  if (i3 != 6) return -6;
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalSignatures_nativeFullArgs(
    // Generated by script (then modified to close argument list):
    //   for i in {0..84}; do echo "    jlong l$((i*3)),"; echo "    jint i$((i*3+2)),"; done
    jlong l0,
    jint i2,
    jlong l3,
    jint i5,
    jlong l6,
    jint i8,
    jlong l9,
    jint i11,
    jlong l12,
    jint i14,
    jlong l15,
    jint i17,
    jlong l18,
    jint i20,
    jlong l21,
    jint i23,
    jlong l24,
    jint i26,
    jlong l27,
    jint i29,
    jlong l30,
    jint i32,
    jlong l33,
    jint i35,
    jlong l36,
    jint i38,
    jlong l39,
    jint i41,
    jlong l42,
    jint i44,
    jlong l45,
    jint i47,
    jlong l48,
    jint i50,
    jlong l51,
    jint i53,
    jlong l54,
    jint i56,
    jlong l57,
    jint i59,
    jlong l60,
    jint i62,
    jlong l63,
    jint i65,
    jlong l66,
    jint i68,
    jlong l69,
    jint i71,
    jlong l72,
    jint i74,
    jlong l75,
    jint i77,
    jlong l78,
    jint i80,
    jlong l81,
    jint i83,
    jlong l84,
    jint i86,
    jlong l87,
    jint i89,
    jlong l90,
    jint i92,
    jlong l93,
    jint i95,
    jlong l96,
    jint i98,
    jlong l99,
    jint i101,
    jlong l102,
    jint i104,
    jlong l105,
    jint i107,
    jlong l108,
    jint i110,
    jlong l111,
    jint i113,
    jlong l114,
    jint i116,
    jlong l117,
    jint i119,
    jlong l120,
    jint i122,
    jlong l123,
    jint i125,
    jlong l126,
    jint i128,
    jlong l129,
    jint i131,
    jlong l132,
    jint i134,
    jlong l135,
    jint i137,
    jlong l138,
    jint i140,
    jlong l141,
    jint i143,
    jlong l144,
    jint i146,
    jlong l147,
    jint i149,
    jlong l150,
    jint i152,
    jlong l153,
    jint i155,
    jlong l156,
    jint i158,
    jlong l159,
    jint i161,
    jlong l162,
    jint i164,
    jlong l165,
    jint i167,
    jlong l168,
    jint i170,
    jlong l171,
    jint i173,
    jlong l174,
    jint i176,
    jlong l177,
    jint i179,
    jlong l180,
    jint i182,
    jlong l183,
    jint i185,
    jlong l186,
    jint i188,
    jlong l189,
    jint i191,
    jlong l192,
    jint i194,
    jlong l195,
    jint i197,
    jlong l198,
    jint i200,
    jlong l201,
    jint i203,
    jlong l204,
    jint i206,
    jlong l207,
    jint i209,
    jlong l210,
    jint i212,
    jlong l213,
    jint i215,
    jlong l216,
    jint i218,
    jlong l219,
    jint i221,
    jlong l222,
    jint i224,
    jlong l225,
    jint i227,
    jlong l228,
    jint i230,
    jlong l231,
    jint i233,
    jlong l234,
    jint i236,
    jlong l237,
    jint i239,
    jlong l240,
    jint i242,
    jlong l243,
    jint i245,
    jlong l246,
    jint i248,
    jlong l249,
    jint i251,
    jlong l252,
    jint i254) {
  jlong l = INT64_C(0xf00000000);
  // Generated by script (then modified to close argument list):
  //   for i in {0..84}; do \
  //     echo "  if (l$((i*3)) != l + $(($i*3))) return -$(($i*3));"; \
  //     echo "  if (i$(($i*3+2)) != $(($i*3+2))) return -$(($i*3+2));"; \
  //  done
  if (l0 != l + 0) return -0;
  if (i2 != 2) return -2;
  if (l3 != l + 3) return -3;
  if (i5 != 5) return -5;
  if (l6 != l + 6) return -6;
  if (i8 != 8) return -8;
  if (l9 != l + 9) return -9;
  if (i11 != 11) return -11;
  if (l12 != l + 12) return -12;
  if (i14 != 14) return -14;
  if (l15 != l + 15) return -15;
  if (i17 != 17) return -17;
  if (l18 != l + 18) return -18;
  if (i20 != 20) return -20;
  if (l21 != l + 21) return -21;
  if (i23 != 23) return -23;
  if (l24 != l + 24) return -24;
  if (i26 != 26) return -26;
  if (l27 != l + 27) return -27;
  if (i29 != 29) return -29;
  if (l30 != l + 30) return -30;
  if (i32 != 32) return -32;
  if (l33 != l + 33) return -33;
  if (i35 != 35) return -35;
  if (l36 != l + 36) return -36;
  if (i38 != 38) return -38;
  if (l39 != l + 39) return -39;
  if (i41 != 41) return -41;
  if (l42 != l + 42) return -42;
  if (i44 != 44) return -44;
  if (l45 != l + 45) return -45;
  if (i47 != 47) return -47;
  if (l48 != l + 48) return -48;
  if (i50 != 50) return -50;
  if (l51 != l + 51) return -51;
  if (i53 != 53) return -53;
  if (l54 != l + 54) return -54;
  if (i56 != 56) return -56;
  if (l57 != l + 57) return -57;
  if (i59 != 59) return -59;
  if (l60 != l + 60) return -60;
  if (i62 != 62) return -62;
  if (l63 != l + 63) return -63;
  if (i65 != 65) return -65;
  if (l66 != l + 66) return -66;
  if (i68 != 68) return -68;
  if (l69 != l + 69) return -69;
  if (i71 != 71) return -71;
  if (l72 != l + 72) return -72;
  if (i74 != 74) return -74;
  if (l75 != l + 75) return -75;
  if (i77 != 77) return -77;
  if (l78 != l + 78) return -78;
  if (i80 != 80) return -80;
  if (l81 != l + 81) return -81;
  if (i83 != 83) return -83;
  if (l84 != l + 84) return -84;
  if (i86 != 86) return -86;
  if (l87 != l + 87) return -87;
  if (i89 != 89) return -89;
  if (l90 != l + 90) return -90;
  if (i92 != 92) return -92;
  if (l93 != l + 93) return -93;
  if (i95 != 95) return -95;
  if (l96 != l + 96) return -96;
  if (i98 != 98) return -98;
  if (l99 != l + 99) return -99;
  if (i101 != 101) return -101;
  if (l102 != l + 102) return -102;
  if (i104 != 104) return -104;
  if (l105 != l + 105) return -105;
  if (i107 != 107) return -107;
  if (l108 != l + 108) return -108;
  if (i110 != 110) return -110;
  if (l111 != l + 111) return -111;
  if (i113 != 113) return -113;
  if (l114 != l + 114) return -114;
  if (i116 != 116) return -116;
  if (l117 != l + 117) return -117;
  if (i119 != 119) return -119;
  if (l120 != l + 120) return -120;
  if (i122 != 122) return -122;
  if (l123 != l + 123) return -123;
  if (i125 != 125) return -125;
  if (l126 != l + 126) return -126;
  if (i128 != 128) return -128;
  if (l129 != l + 129) return -129;
  if (i131 != 131) return -131;
  if (l132 != l + 132) return -132;
  if (i134 != 134) return -134;
  if (l135 != l + 135) return -135;
  if (i137 != 137) return -137;
  if (l138 != l + 138) return -138;
  if (i140 != 140) return -140;
  if (l141 != l + 141) return -141;
  if (i143 != 143) return -143;
  if (l144 != l + 144) return -144;
  if (i146 != 146) return -146;
  if (l147 != l + 147) return -147;
  if (i149 != 149) return -149;
  if (l150 != l + 150) return -150;
  if (i152 != 152) return -152;
  if (l153 != l + 153) return -153;
  if (i155 != 155) return -155;
  if (l156 != l + 156) return -156;
  if (i158 != 158) return -158;
  if (l159 != l + 159) return -159;
  if (i161 != 161) return -161;
  if (l162 != l + 162) return -162;
  if (i164 != 164) return -164;
  if (l165 != l + 165) return -165;
  if (i167 != 167) return -167;
  if (l168 != l + 168) return -168;
  if (i170 != 170) return -170;
  if (l171 != l + 171) return -171;
  if (i173 != 173) return -173;
  if (l174 != l + 174) return -174;
  if (i176 != 176) return -176;
  if (l177 != l + 177) return -177;
  if (i179 != 179) return -179;
  if (l180 != l + 180) return -180;
  if (i182 != 182) return -182;
  if (l183 != l + 183) return -183;
  if (i185 != 185) return -185;
  if (l186 != l + 186) return -186;
  if (i188 != 188) return -188;
  if (l189 != l + 189) return -189;
  if (i191 != 191) return -191;
  if (l192 != l + 192) return -192;
  if (i194 != 194) return -194;
  if (l195 != l + 195) return -195;
  if (i197 != 197) return -197;
  if (l198 != l + 198) return -198;
  if (i200 != 200) return -200;
  if (l201 != l + 201) return -201;
  if (i203 != 203) return -203;
  if (l204 != l + 204) return -204;
  if (i206 != 206) return -206;
  if (l207 != l + 207) return -207;
  if (i209 != 209) return -209;
  if (l210 != l + 210) return -210;
  if (i212 != 212) return -212;
  if (l213 != l + 213) return -213;
  if (i215 != 215) return -215;
  if (l216 != l + 216) return -216;
  if (i218 != 218) return -218;
  if (l219 != l + 219) return -219;
  if (i221 != 221) return -221;
  if (l222 != l + 222) return -222;
  if (i224 != 224) return -224;
  if (l225 != l + 225) return -225;
  if (i227 != 227) return -227;
  if (l228 != l + 228) return -228;
  if (i230 != 230) return -230;
  if (l231 != l + 231) return -231;
  if (i233 != 233) return -233;
  if (l234 != l + 234) return -234;
  if (i236 != 236) return -236;
  if (l237 != l + 237) return -237;
  if (i239 != 239) return -239;
  if (l240 != l + 240) return -240;
  if (i242 != 242) return -242;
  if (l243 != l + 243) return -243;
  if (i245 != 245) return -245;
  if (l246 != l + 246) return -246;
  if (i248 != 248) return -248;
  if (l249 != l + 249) return -249;
  if (i251 != 251) return -251;
  if (l252 != l + 252) return -252;
  if (i254 != 254) return -254;
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalClinitCheck_nativeMethodVoid() {
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalClinitCheck_nativeMethod(jint i) {
  return i;
}

extern "C" JNIEXPORT jint JNICALL Java_CriticalClinitCheck_nativeMethodWithManyParameters(
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  bool ok = VerifyManyParameters(
      i1, l1, f1, d1,
      i2, l2, f2, d2,
      i3, l3, f3, d3,
      i4, l4, f4, d4,
      i5, l5, f5, d5,
      i6, l6, f6, d6,
      i7, l7, f7, d7,
      i8, l8, f8, d8);
  return ok ? 42 : -1;
}

}  // namespace art
