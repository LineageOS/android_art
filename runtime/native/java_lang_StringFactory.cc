/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "java_lang_StringFactory.h"

#include "common_throws.h"
#include "handle_scope-inl.h"
#include "jni/jni_internal.h"
#include "mirror/object-inl.h"
#include "mirror/string-alloc-inl.h"
#include "native_util.h"
#include "nativehelper/jni_macros.h"
#include "nativehelper/scoped_local_ref.h"
#include "nativehelper/scoped_primitive_array.h"
#include "scoped_fast_native_object_access-inl.h"
#include "scoped_thread_state_change-inl.h"

namespace art {

static jstring StringFactory_newStringFromBytes(JNIEnv* env, jclass, jbyteArray java_data,
                                                jint high, jint offset, jint byte_count) {
  ScopedFastNativeObjectAccess soa(env);
  if (UNLIKELY(java_data == nullptr)) {
    ThrowNullPointerException("data == null");
    return nullptr;
  }
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ByteArray> byte_array(hs.NewHandle(soa.Decode<mirror::ByteArray>(java_data)));
  int32_t data_size = byte_array->GetLength();
  if ((offset | byte_count) < 0 || byte_count > data_size - offset) {
    soa.Self()->ThrowNewExceptionF("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "length=%d; regionStart=%d; regionLength=%d", data_size,
                                   offset, byte_count);
    return nullptr;
  }
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  ObjPtr<mirror::String> result = mirror::String::AllocFromByteArray(soa.Self(),
                                                                     byte_count,
                                                                     byte_array,
                                                                     offset,
                                                                     high,
                                                                     allocator_type);
  return soa.AddLocalReference<jstring>(result);
}

// The char array passed as `java_data` must not be a null reference.
static jstring StringFactory_newStringFromChars(JNIEnv* env, jclass, jint offset,
                                                jint char_count, jcharArray java_data) {
  DCHECK(java_data != nullptr);
  ScopedFastNativeObjectAccess soa(env);
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::CharArray> char_array(hs.NewHandle(soa.Decode<mirror::CharArray>(java_data)));
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  ObjPtr<mirror::String> result = mirror::String::AllocFromCharArray(soa.Self(),
                                                                     char_count,
                                                                     char_array,
                                                                     offset,
                                                                     allocator_type);
  return soa.AddLocalReference<jstring>(result);
}

static jstring StringFactory_newStringFromString(JNIEnv* env, jclass, jstring to_copy) {
  ScopedFastNativeObjectAccess soa(env);
  if (UNLIKELY(to_copy == nullptr)) {
    ThrowNullPointerException("toCopy == null");
    return nullptr;
  }
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::String> string(hs.NewHandle(soa.Decode<mirror::String>(to_copy)));
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  ObjPtr<mirror::String> result = mirror::String::AllocFromString(soa.Self(),
                                                                  string->GetLength(),
                                                                  string,
                                                                  /*offset=*/ 0,
                                                                  allocator_type);
  return soa.AddLocalReference<jstring>(result);
}

static jstring StringFactory_newStringFromUtf8Bytes(JNIEnv* env, jclass, jbyteArray java_data,
                                                    jint offset, jint byte_count) {
  // Local Define in here
  static const jchar kReplacementChar = 0xfffd;
  static const int kDefaultBufferSize = 256;
  static const int kTableUtf8Needed[] = {
    //      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0xc0 - 0xcf
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0xd0 - 0xdf
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 0xe0 - 0xef
    3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0xf0 - 0xff
  };

  ScopedFastNativeObjectAccess soa(env);
  if (UNLIKELY(java_data == nullptr)) {
    ThrowNullPointerException("data == null");
    return nullptr;
  }

  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ByteArray> byte_array(hs.NewHandle(soa.Decode<mirror::ByteArray>(java_data)));
  int32_t data_size = byte_array->GetLength();
  if ((offset | byte_count) < 0 || byte_count > data_size - offset) {
    soa.Self()->ThrowNewExceptionF("Ljava/lang/StringIndexOutOfBoundsException;",
        "length=%d; regionStart=%d; regionLength=%d", data_size,
        offset, byte_count);
    return nullptr;
  }

  /*
   * This code converts a UTF-8 byte sequence to a Java String (UTF-16).
   * It implements the W3C recommended UTF-8 decoder.
   * https://www.w3.org/TR/encoding/#utf-8-decoder
   *
   * Unicode 3.2 Well-Formed UTF-8 Byte Sequences
   * Code Points        First  Second Third Fourth
   * U+0000..U+007F     00..7F
   * U+0080..U+07FF     C2..DF 80..BF
   * U+0800..U+0FFF     E0     A0..BF 80..BF
   * U+1000..U+CFFF     E1..EC 80..BF 80..BF
   * U+D000..U+D7FF     ED     80..9F 80..BF
   * U+E000..U+FFFF     EE..EF 80..BF 80..BF
   * U+10000..U+3FFFF   F0     90..BF 80..BF 80..BF
   * U+40000..U+FFFFF   F1..F3 80..BF 80..BF 80..BF
   * U+100000..U+10FFFF F4     80..8F 80..BF 80..BF
   *
   * Please refer to Unicode as the authority.
   * p.126 Table 3-7 in http://www.unicode.org/versions/Unicode10.0.0/ch03.pdf
   *
   * Handling Malformed Input
   * The maximal subpart should be replaced by a single U+FFFD. Maximal subpart is
   * the longest code unit subsequence starting at an unconvertible offset that is either
   * 1) the initial subsequence of a well-formed code unit sequence, or
   * 2) a subsequence of length one:
   * One U+FFFD should be emitted for every sequence of bytes that is an incomplete prefix
   * of a valid sequence, and with the conversion to restart after the incomplete sequence.
   *
   * For example, in byte sequence "41 C0 AF 41 F4 80 80 41", the maximal subparts are
   * "C0", "AF", and "F4 80 80". "F4 80 80" can be the initial subsequence of "F4 80 80 80",
   * but "C0" can't be the initial subsequence of any well-formed code unit sequence.
   * Thus, the output should be "A\ufffd\ufffdA\ufffdA".
   *
   * Please refer to section "Best Practices for Using U+FFFD." in
   * http://www.unicode.org/versions/Unicode10.0.0/ch03.pdf
   */

  // Initial value
  jchar stack_buffer[kDefaultBufferSize];
  std::unique_ptr<jchar[]> allocated_buffer;
  jchar* v;
  if (byte_count <= kDefaultBufferSize) {
    v = stack_buffer;
  } else {
    allocated_buffer.reset(new jchar[byte_count]);
    v = allocated_buffer.get();
  }

  jbyte* d = byte_array->GetData();
  DCHECK(d != nullptr);

  int idx = offset;
  int last = offset + byte_count;
  int s = 0;

  int code_point = 0;
  int utf8_bytes_seen = 0;
  int utf8_bytes_needed = 0;
  int lower_bound = 0x80;
  int upper_bound = 0xbf;
  while (idx < last) {
    int b = d[idx++] & 0xff;
    if (utf8_bytes_needed == 0) {
      if ((b & 0x80) == 0) {  // ASCII char. 0xxxxxxx
        v[s++] = (jchar) b;
        continue;
      }

      if ((b & 0x40) == 0) {  // 10xxxxxx is illegal as first byte
        v[s++] = kReplacementChar;
        continue;
      }

      // 11xxxxxx
      int tableLookupIndex = b & 0x3f;
      utf8_bytes_needed = kTableUtf8Needed[tableLookupIndex];
      if (utf8_bytes_needed == 0) {
        v[s++] = kReplacementChar;
        continue;
      }

      // utf8_bytes_needed
      // 1: b & 0x1f
      // 2: b & 0x0f
      // 3: b & 0x07
      code_point = b & (0x3f >> utf8_bytes_needed);
      if (b == 0xe0) {
        lower_bound = 0xa0;
      } else if (b == 0xed) {
        upper_bound = 0x9f;
      } else if (b == 0xf0) {
        lower_bound = 0x90;
      } else if (b == 0xf4) {
        upper_bound = 0x8f;
      }
    } else {
      if (b < lower_bound || b > upper_bound) {
        // The bytes seen are ill-formed. Substitute them with U+FFFD
        v[s++] = kReplacementChar;
        code_point = 0;
        utf8_bytes_needed = 0;
        utf8_bytes_seen = 0;
        lower_bound = 0x80;
        upper_bound = 0xbf;
        /*
         * According to the Unicode Standard,
         * "a UTF-8 conversion process is required to never consume well-formed
         * subsequences as part of its error handling for ill-formed subsequences"
         * The current byte could be part of well-formed subsequences. Reduce the
         * index by 1 to parse it in next loop.
         */
        idx--;
        continue;
      }

      lower_bound = 0x80;
      upper_bound = 0xbf;
      code_point = (code_point << 6) | (b & 0x3f);
      utf8_bytes_seen++;
      if (utf8_bytes_needed != utf8_bytes_seen) {
        continue;
      }

      // Encode chars from U+10000 up as surrogate pairs
      if (code_point < 0x10000) {
        v[s++] = (jchar) code_point;
      } else {
        v[s++] = (jchar) ((code_point >> 10) + 0xd7c0);
        v[s++] = (jchar) ((code_point & 0x3ff) + 0xdc00);
      }

      utf8_bytes_seen = 0;
      utf8_bytes_needed = 0;
      code_point = 0;
    }
  }

  // The bytes seen are ill-formed. Substitute them by U+FFFD
  if (utf8_bytes_needed != 0) {
    v[s++] = kReplacementChar;
  }

  ObjPtr<mirror::String> result = mirror::String::AllocFromUtf16(soa.Self(), s, v);
  return soa.AddLocalReference<jstring>(result);
}

static JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(StringFactory, newStringFromBytes, "([BIII)Ljava/lang/String;"),
  FAST_NATIVE_METHOD(StringFactory, newStringFromChars, "(II[C)Ljava/lang/String;"),
  FAST_NATIVE_METHOD(StringFactory, newStringFromString, "(Ljava/lang/String;)Ljava/lang/String;"),
  FAST_NATIVE_METHOD(StringFactory, newStringFromUtf8Bytes, "([BII)Ljava/lang/String;"),
};

void register_java_lang_StringFactory(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("java/lang/StringFactory");
}

}  // namespace art
