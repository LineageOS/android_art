/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include "libcore_util_CharsetUtils.h"

#include <string.h>

#include "handle_scope-inl.h"
#include "jni/jni_internal.h"
#include "mirror/string-inl.h"
#include "mirror/string.h"
#include "native_util.h"
#include "nativehelper/scoped_primitive_array.h"
#include "nativehelper/jni_macros.h"
#include "scoped_fast_native_object_access-inl.h"
#include "unicode/utf16.h"

namespace art {

static void CharsetUtils_asciiBytesToChars(JNIEnv* env, jclass, jbyteArray javaBytes, jint offset,
                                           jint length, jcharArray javaChars) {
  ScopedByteArrayRO bytes(env, javaBytes);
  if (bytes.get() == nullptr) {
    return;
  }
  ScopedCharArrayRW chars(env, javaChars);
  if (chars.get() == nullptr) {
    return;
  }

  const jbyte* src = &bytes[offset];
  jchar* dst = &chars[0];
  static const jchar REPLACEMENT_CHAR = 0xfffd;
  for (int i = length - 1; i >= 0; --i) {
    jchar ch = static_cast<jchar>(*src++ & 0xff);
    *dst++ = (ch <= 0x7f) ? ch : REPLACEMENT_CHAR;
  }
}

static void CharsetUtils_isoLatin1BytesToChars(JNIEnv* env, jclass, jbyteArray javaBytes,
                                               jint offset, jint length, jcharArray javaChars) {
  ScopedByteArrayRO bytes(env, javaBytes);
  if (bytes.get() == nullptr) {
    return;
  }
  ScopedCharArrayRW chars(env, javaChars);
  if (chars.get() == nullptr) {
    return;
  }

  const jbyte* src = &bytes[offset];
  jchar* dst = &chars[0];
  for (int i = length - 1; i >= 0; --i) {
    *dst++ = static_cast<jchar>(*src++ & 0xff);
  }
}

/**
 * Translates the given characters to US-ASCII or ISO-8859-1 bytes, using the fact that
 * Unicode code points between U+0000 and U+007f inclusive are identical to US-ASCII, while
 * U+0000 to U+00ff inclusive are identical to ISO-8859-1.
 */
static jbyteArray charsToBytes(JNIEnv* env, jstring java_string, jint offset, jint length,
                               jchar maxValidChar) {
  ScopedFastNativeObjectAccess soa(env);
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::String> string(hs.NewHandle(soa.Decode<mirror::String>(java_string)));
  if (string == nullptr) {
    return nullptr;
  }

  ObjPtr<mirror::ByteArray> result = mirror::ByteArray::Alloc(soa.Self(), length);
  if (result == nullptr) {
    return nullptr;
  }

  if (string->IsCompressed()) {
    // All characters in a compressed string are ASCII and therefore do not need a replacement.
    DCHECK_GE(maxValidChar, 0x7f);
    memcpy(result->GetData(), string->GetValueCompressed() + offset, length);
  } else {
    const uint16_t* src = string->GetValue() + offset;
    auto clamp = [maxValidChar](uint16_t c) {
      return static_cast<jbyte>(dchecked_integral_cast<uint8_t>((c > maxValidChar) ? '?' : c));
    };
    std::transform(src, src + length, result->GetData(), clamp);
  }
  return soa.AddLocalReference<jbyteArray>(result);
}

static jbyteArray CharsetUtils_toAsciiBytes(JNIEnv* env, jclass, jstring java_string, jint offset,
                                            jint length) {
    return charsToBytes(env, java_string, offset, length, 0x7f);
}

static jbyteArray CharsetUtils_toIsoLatin1Bytes(JNIEnv* env, jclass, jstring java_string,
                                                jint offset, jint length) {
    return charsToBytes(env, java_string, offset, length, 0xff);
}

static jbyteArray CharsetUtils_toUtf8Bytes(JNIEnv* env, jclass, jstring java_string, jint offset,
                                           jint length) {
  ScopedFastNativeObjectAccess soa(env);
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::String> string(hs.NewHandle(soa.Decode<mirror::String>(java_string)));
  if (string == nullptr) {
    return nullptr;
  }

  DCHECK_GE(offset, 0);
  DCHECK_LE(offset, string->GetLength());
  DCHECK_GE(length, 0);
  DCHECK_LE(length, string->GetLength() - offset);

  auto visit_chars16 = [string, offset, length](auto append) REQUIRES_SHARED(Locks::mutator_lock_) {
    const uint16_t* chars16 = string->GetValue() + offset;
    for (int i = 0; i < length; ++i) {
      jint ch = chars16[i];
      if (ch < 0x80) {
        // One byte.
        append(ch);
      } else if (ch < 0x800) {
        // Two bytes.
        append((ch >> 6) | 0xc0);
        append((ch & 0x3f) | 0x80);
      } else if (U16_IS_SURROGATE(ch)) {
        // A supplementary character.
        jchar high = static_cast<jchar>(ch);
        jchar low = (i + 1 != length) ? chars16[i + 1] : 0;
        if (!U16_IS_SURROGATE_LEAD(high) || !U16_IS_SURROGATE_TRAIL(low)) {
          append('?');
          continue;
        }
        // Now we know we have a *valid* surrogate pair, we can consume the low surrogate.
        ++i;
        ch = U16_GET_SUPPLEMENTARY(high, low);
        // Four bytes.
        append((ch >> 18) | 0xf0);
        append(((ch >> 12) & 0x3f) | 0x80);
        append(((ch >> 6) & 0x3f) | 0x80);
        append((ch & 0x3f) | 0x80);
      } else {
        // Three bytes.
        append((ch >> 12) | 0xe0);
        append(((ch >> 6) & 0x3f) | 0x80);
        append((ch & 0x3f) | 0x80);
      }
    }
  };

  bool compressed = string->IsCompressed();
  size_t utf8_length = 0;
  if (compressed) {
    utf8_length = length;
  } else {
    visit_chars16([&utf8_length](jbyte c ATTRIBUTE_UNUSED) { ++utf8_length; });
  }
  ObjPtr<mirror::ByteArray> result =
      mirror::ByteArray::Alloc(soa.Self(), dchecked_integral_cast<int32_t>(utf8_length));
  if (result == nullptr) {
    return nullptr;
  }

  if (compressed) {
    memcpy(result->GetData(), string->GetValueCompressed() + offset, length);
  } else {
    int8_t* data = result->GetData();
    visit_chars16([&data](jbyte c) { *data++ = c; });
  }
  return soa.AddLocalReference<jbyteArray>(result);
}

static JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(CharsetUtils, asciiBytesToChars, "([BII[C)V"),
  FAST_NATIVE_METHOD(CharsetUtils, isoLatin1BytesToChars, "([BII[C)V"),
  FAST_NATIVE_METHOD(CharsetUtils, toAsciiBytes, "(Ljava/lang/String;II)[B"),
  FAST_NATIVE_METHOD(CharsetUtils, toIsoLatin1Bytes, "(Ljava/lang/String;II)[B"),
  FAST_NATIVE_METHOD(CharsetUtils, toUtf8Bytes, "(Ljava/lang/String;II)[B"),
};

void register_libcore_util_CharsetUtils(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("libcore/util/CharsetUtils");
}

}  // namespace art
