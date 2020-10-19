/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "string-alloc-inl.h"

#include "arch/memcmp16.h"
#include "array-alloc-inl.h"
#include "base/array_ref.h"
#include "base/stl_util.h"
#include "class-inl.h"
#include "dex/descriptors_names.h"
#include "dex/utf-inl.h"
#include "gc/accounting/card_table-inl.h"
#include "handle_scope-inl.h"
#include "intern_table.h"
#include "object-inl.h"
#include "runtime.h"
#include "string-inl.h"
#include "thread.h"

namespace art {
namespace mirror {

int32_t String::FastIndexOf(int32_t ch, int32_t start) {
  int32_t count = GetLength();
  if (start < 0) {
    start = 0;
  } else if (start > count) {
    start = count;
  }
  if (IsCompressed()) {
    return FastIndexOf<uint8_t>(GetValueCompressed(), ch, start);
  } else {
    return FastIndexOf<uint16_t>(GetValue(), ch, start);
  }
}

int String::ComputeHashCode() {
  int32_t hash_code = 0;
  if (IsCompressed()) {
    hash_code = ComputeUtf16Hash(GetValueCompressed(), GetLength());
  } else {
    hash_code = ComputeUtf16Hash(GetValue(), GetLength());
  }
  SetHashCode(hash_code);
  return hash_code;
}

inline bool String::AllASCIIExcept(const uint16_t* chars, int32_t length, uint16_t non_ascii) {
  DCHECK(!IsASCII(non_ascii));
  for (int32_t i = 0; i < length; ++i) {
    if (!IsASCII(chars[i]) && chars[i] != non_ascii) {
      return false;
    }
  }
  return true;
}

ObjPtr<String> String::DoReplace(Thread* self, Handle<String> src, uint16_t old_c, uint16_t new_c) {
  int32_t length = src->GetLength();
  DCHECK(src->IsCompressed()
             ? ContainsElement(ArrayRef<uint8_t>(src->value_compressed_, length), old_c)
             : ContainsElement(ArrayRef<uint16_t>(src->value_, length), old_c));
  bool compressible =
      kUseStringCompression &&
      IsASCII(new_c) &&
      (src->IsCompressed() || (!IsASCII(old_c) && AllASCIIExcept(src->value_, length, old_c)));
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  const int32_t length_with_flag = String::GetFlaggedCount(length, compressible);

  auto visitor = [=](ObjPtr<Object> obj, size_t usable_size) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetStringCountVisitor set_string_count_visitor(length_with_flag);
    set_string_count_visitor(obj, usable_size);
    ObjPtr<String> new_string = obj->AsString();
    if (compressible) {
      auto replace = [old_c, new_c](uint16_t c) {
        return dchecked_integral_cast<uint8_t>((old_c != c) ? c : new_c);
      };
      uint8_t* out = new_string->value_compressed_;
      if (LIKELY(src->IsCompressed())) {  // LIKELY(compressible == src->IsCompressed())
        std::transform(src->value_compressed_, src->value_compressed_ + length, out, replace);
      } else {
        std::transform(src->value_, src->value_ + length, out, replace);
      }
      DCHECK(kUseStringCompression && AllASCII(out, length));
    } else {
      auto replace = [old_c, new_c](uint16_t c) {
        return (old_c != c) ? c : new_c;
      };
      uint16_t* out = new_string->value_;
      if (UNLIKELY(src->IsCompressed())) {  // LIKELY(compressible == src->IsCompressed())
        std::transform(src->value_compressed_, src->value_compressed_ + length, out, replace);
      } else {
        std::transform(src->value_, src->value_ + length, out, replace);
      }
      DCHECK(!kUseStringCompression || !AllASCII(out, length));
    }
  };
  return Alloc(self, length_with_flag, allocator_type, visitor);
}

ObjPtr<String> String::AllocFromStrings(Thread* self,
                                        Handle<String> string,
                                        Handle<String> string2) {
  int32_t length = string->GetLength();
  int32_t length2 = string2->GetLength();
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  const bool compressible = kUseStringCompression &&
      (string->IsCompressed() && string2->IsCompressed());
  const int32_t length_with_flag = String::GetFlaggedCount(length + length2, compressible);

  auto visitor = [=](ObjPtr<Object> obj, size_t usable_size) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetStringCountVisitor set_string_count_visitor(length_with_flag);
    set_string_count_visitor(obj, usable_size);
    ObjPtr<String> new_string = obj->AsString();
    if (compressible) {
      uint8_t* new_value = new_string->GetValueCompressed();
      memcpy(new_value, string->GetValueCompressed(), length * sizeof(uint8_t));
      memcpy(new_value + length, string2->GetValueCompressed(), length2 * sizeof(uint8_t));
    } else {
      uint16_t* new_value = new_string->GetValue();
      if (string->IsCompressed()) {
        const uint8_t* value = string->GetValueCompressed();
        for (int i = 0; i < length; ++i) {
          new_value[i] = value[i];
        }
      } else {
        memcpy(new_value, string->GetValue(), length * sizeof(uint16_t));
      }
      if (string2->IsCompressed()) {
        const uint8_t* value2 = string->GetValueCompressed();
        for (int i = 0; i < length2; ++i) {
          new_value[i+length] = value2[i];
        }
      } else {
        memcpy(new_value + length, string2->GetValue(), length2 * sizeof(uint16_t));
      }
    }
  };
  return Alloc(self, length_with_flag, allocator_type, visitor);
}

ObjPtr<String> String::AllocFromUtf16(Thread* self,
                                      int32_t utf16_length,
                                      const uint16_t* utf16_data_in) {
  CHECK(utf16_data_in != nullptr || utf16_length == 0);
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  const bool compressible = kUseStringCompression &&
                            String::AllASCII<uint16_t>(utf16_data_in, utf16_length);
  int32_t length_with_flag = String::GetFlaggedCount(utf16_length, compressible);

  auto visitor = [=](ObjPtr<Object> obj, size_t usable_size) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetStringCountVisitor set_string_count_visitor(length_with_flag);
    set_string_count_visitor(obj, usable_size);
    ObjPtr<String> new_string = obj->AsString();
    if (compressible) {
      uint8_t* value = new_string->GetValueCompressed();
      for (int i = 0; i < utf16_length; ++i) {
        value[i] = static_cast<uint8_t>(utf16_data_in[i]);
      }
    } else {
      memcpy(new_string->GetValue(), utf16_data_in, utf16_length * sizeof(uint16_t));
    }
  };
  return Alloc(self, length_with_flag, allocator_type, visitor);
}

ObjPtr<String> String::AllocFromModifiedUtf8(Thread* self, const char* utf) {
  DCHECK(utf != nullptr);
  size_t byte_count = strlen(utf);
  size_t char_count = CountModifiedUtf8Chars(utf, byte_count);
  return AllocFromModifiedUtf8(self, char_count, utf, byte_count);
}

ObjPtr<String> String::AllocFromModifiedUtf8(Thread* self,
                                             int32_t utf16_length,
                                             const char* utf8_data_in) {
  return AllocFromModifiedUtf8(self, utf16_length, utf8_data_in, strlen(utf8_data_in));
}

ObjPtr<String> String::AllocFromModifiedUtf8(Thread* self,
                                             int32_t utf16_length,
                                             const char* utf8_data_in,
                                             int32_t utf8_length) {
  gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
  const bool compressible = kUseStringCompression && (utf16_length == utf8_length);
  const int32_t length_with_flag = String::GetFlaggedCount(utf16_length, compressible);

  auto visitor = [=](ObjPtr<Object> obj, size_t usable_size) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetStringCountVisitor set_string_count_visitor(length_with_flag);
    set_string_count_visitor(obj, usable_size);
    ObjPtr<String> new_string = obj->AsString();
    if (compressible) {
      memcpy(new_string->GetValueCompressed(), utf8_data_in, utf16_length * sizeof(uint8_t));
    } else {
      uint16_t* utf16_data_out = new_string->GetValue();
      ConvertModifiedUtf8ToUtf16(utf16_data_out, utf16_length, utf8_data_in, utf8_length);
    }
  };
  return Alloc(self, length_with_flag, allocator_type, visitor);
}

bool String::Equals(ObjPtr<String> that) {
  if (this == that) {
    // Quick reference equality test
    return true;
  } else if (that == nullptr) {
    // Null isn't an instanceof anything
    return false;
  } else if (this->GetCount() != that->GetCount()) {
    // Quick length and compression inequality test
    return false;
  } else {
    // Note: don't short circuit on hash code as we're presumably here as the
    // hash code was already equal
    if (this->IsCompressed()) {
      return memcmp(this->GetValueCompressed(), that->GetValueCompressed(), this->GetLength()) == 0;
    } else {
      return memcmp(this->GetValue(), that->GetValue(), sizeof(uint16_t) * this->GetLength()) == 0;
    }
  }
}

bool String::Equals(const char* modified_utf8) {
  const int32_t length = GetLength();
  if (IsCompressed()) {
    return strlen(modified_utf8) == dchecked_integral_cast<uint32_t>(length) &&
           memcmp(modified_utf8, GetValueCompressed(), length) == 0;
  }
  const uint16_t* value = GetValue();
  int32_t i = 0;
  while (i < length) {
    const uint32_t ch = GetUtf16FromUtf8(&modified_utf8);
    if (ch == '\0') {
      return false;
    }

    if (GetLeadingUtf16Char(ch) != value[i++]) {
      return false;
    }

    const uint16_t trailing = GetTrailingUtf16Char(ch);
    if (trailing != 0) {
      if (i == length) {
        return false;
      }

      if (value[i++] != trailing) {
        return false;
      }
    }
  }
  return *modified_utf8 == '\0';
}

// Create a modified UTF-8 encoded std::string from a java/lang/String object.
std::string String::ToModifiedUtf8() {
  if (IsCompressed()) {
    return std::string(reinterpret_cast<const char*>(GetValueCompressed()), GetLength());
  } else {
    size_t byte_count = GetUtfLength();
    std::string result(byte_count, static_cast<char>(0));
    ConvertUtf16ToModifiedUtf8(&result[0], byte_count, GetValue(), GetLength());
    return result;
  }
}

int32_t String::CompareTo(ObjPtr<String> rhs) {
  // Quick test for comparison of a string with itself.
  ObjPtr<String> lhs = this;
  if (lhs == rhs) {
    return 0;
  }
  int32_t lhs_count = lhs->GetLength();
  int32_t rhs_count = rhs->GetLength();
  int32_t count_diff = lhs_count - rhs_count;
  int32_t min_count = (count_diff < 0) ? lhs_count : rhs_count;
  if (lhs->IsCompressed() && rhs->IsCompressed()) {
    const uint8_t* lhs_chars = lhs->GetValueCompressed();
    const uint8_t* rhs_chars = rhs->GetValueCompressed();
    for (int32_t i = 0; i < min_count; ++i) {
      int32_t char_diff = static_cast<int32_t>(lhs_chars[i]) - static_cast<int32_t>(rhs_chars[i]);
      if (char_diff != 0) {
        return char_diff;
      }
    }
  } else if (lhs->IsCompressed() || rhs->IsCompressed()) {
    const uint8_t* compressed_chars =
        lhs->IsCompressed() ? lhs->GetValueCompressed() : rhs->GetValueCompressed();
    const uint16_t* uncompressed_chars = lhs->IsCompressed() ? rhs->GetValue() : lhs->GetValue();
    for (int32_t i = 0; i < min_count; ++i) {
      int32_t char_diff =
          static_cast<int32_t>(compressed_chars[i]) - static_cast<int32_t>(uncompressed_chars[i]);
      if (char_diff != 0) {
        return lhs->IsCompressed() ? char_diff : -char_diff;
      }
    }
  } else {
    const uint16_t* lhs_chars = lhs->GetValue();
    const uint16_t* rhs_chars = rhs->GetValue();
    // FIXME: The MemCmp16() name is misleading. It returns the char difference on mismatch
    // where memcmp() only guarantees that the returned value has the same sign.
    int32_t char_diff = MemCmp16(lhs_chars, rhs_chars, min_count);
    if (char_diff != 0) {
      return char_diff;
    }
  }
  return count_diff;
}

ObjPtr<CharArray> String::ToCharArray(Handle<String> h_this, Thread* self) {
  ObjPtr<CharArray> result = CharArray::Alloc(self, h_this->GetLength());
  if (result != nullptr) {
    if (h_this->IsCompressed()) {
      int32_t length = h_this->GetLength();
      const uint8_t* src = h_this->GetValueCompressed();
      uint16_t* dest = result->GetData();
      for (int i = 0; i < length; ++i) {
        dest[i] = src[i];
      }
    } else {
      memcpy(result->GetData(), h_this->GetValue(), h_this->GetLength() * sizeof(uint16_t));
    }
  } else {
    self->AssertPendingOOMException();
  }
  return result;
}

void String::GetChars(int32_t start, int32_t end, Handle<CharArray> array, int32_t index) {
  uint16_t* data = array->GetData() + index;
  DCHECK_LE(start, end);
  int32_t length = end - start;
  if (IsCompressed()) {
    const uint8_t* value = GetValueCompressed() + start;
    for (int i = 0; i < length; ++i) {
      data[i] = value[i];
    }
  } else {
    uint16_t* value = GetValue() + start;
    memcpy(data, value, length * sizeof(uint16_t));
  }
}

bool String::IsValueNull() {
  return (IsCompressed()) ? (GetValueCompressed() == nullptr) : (GetValue() == nullptr);
}

std::string String::PrettyStringDescriptor(ObjPtr<mirror::String> java_descriptor) {
  if (java_descriptor == nullptr) {
    return "null";
  }
  return java_descriptor->PrettyStringDescriptor();
}

std::string String::PrettyStringDescriptor() {
  return PrettyDescriptor(ToModifiedUtf8().c_str());
}

ObjPtr<String> String::Intern() {
  return Runtime::Current()->GetInternTable()->InternWeak(this);
}

}  // namespace mirror
}  // namespace art
