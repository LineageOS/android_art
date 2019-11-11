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

#ifndef ART_RUNTIME_REFLECTIVE_VALUE_VISITOR_H_
#define ART_RUNTIME_REFLECTIVE_VALUE_VISITOR_H_

#include <android-base/logging.h>

#include <array>
#include <compare>
#include <functional>
#include <stack>

#include "android-base/macros.h"
#include "base/enums.h"
#include "base/globals.h"
#include "base/locks.h"
#include "base/macros.h"
#include "base/value_object.h"
#include "dex/dex_file.h"
#include "jni.h"
#include "mirror/dex_cache.h"
#include "obj_ptr.h"

namespace art {

class ArtField;
class ArtMethod;
class BaseReflectiveHandleScope;
class Thread;

class ReflectionSourceInfo;

class ReflectiveValueVisitor : public ValueObject {
 public:
  virtual ~ReflectiveValueVisitor() {}

  virtual ArtMethod* VisitMethod(ArtMethod* in, const ReflectionSourceInfo& info)
      REQUIRES_SHARED(Locks::mutator_lock_) = 0;
  virtual ArtField* VisitField(ArtField* in, const ReflectionSourceInfo& info)
      REQUIRES_SHARED(Locks::mutator_lock_) = 0;

  // Give it an entrypoint through operator() to interact with things that expect lambda-like things
  template <typename T,
            typename = typename std::enable_if<std::is_same_v<T, ArtField> ||
                                               std::is_same_v<T, ArtMethod>>>
  T* operator()(T* t, const ReflectionSourceInfo& info) REQUIRES_SHARED(Locks::mutator_lock_) {
    if constexpr (std::is_same_v<T, ArtField>) {
      return VisitField(t, info);
    } else {
      static_assert(std::is_same_v<T, ArtMethod>, "Expected ArtField or ArtMethod");
      return VisitMethod(t, info);
    }
  }
};

template <typename FieldVis, typename MethodVis>
class FunctionReflectiveValueVisitor : public ReflectiveValueVisitor {
 public:
  FunctionReflectiveValueVisitor(FieldVis fv, MethodVis mv) : fv_(fv), mv_(mv) {}
  ArtField* VisitField(ArtField* in, const ReflectionSourceInfo& info) override
      REQUIRES(Locks::mutator_lock_) {
    return fv_(in, info);
  }
  ArtMethod* VisitMethod(ArtMethod* in, const ReflectionSourceInfo& info) override
      REQUIRES(Locks::mutator_lock_) {
    return mv_(in, info);
  }

 private:
  FieldVis fv_;
  MethodVis mv_;
};

enum ReflectionSourceType {
  kSourceUnknown = 0,
  kSourceJavaLangReflectExecutable,
  kSourceJavaLangReflectField,
  kSourceJavaLangInvokeMethodHandle,
  kSourceJavaLangInvokeFieldVarHandle,
  kSourceThreadHandleScope,
  kSourceJniFieldId,
  kSourceJniMethodId,
  kSourceDexCacheResolvedMethod,
  kSourceDexCacheResolvedField,
  kSourceMiscInternal,
};
std::ostream& operator<<(std::ostream& os, const ReflectionSourceType& type);

class ReflectionSourceInfo : public ValueObject {
 public:
  virtual ~ReflectionSourceInfo() {}
  // Thread id 0 is for non thread roots.
  explicit ReflectionSourceInfo(ReflectionSourceType type) : type_(type) {}
  virtual void Describe(std::ostream& os) const {
    os << "Type=" << type_;
  }

  ReflectionSourceType GetType() const {
    return type_;
  }

 private:
  const ReflectionSourceType type_;

  DISALLOW_COPY_AND_ASSIGN(ReflectionSourceInfo);
};
inline std::ostream& operator<<(std::ostream& os, const ReflectionSourceInfo& info) {
  info.Describe(os);
  return os;
}

class ReflectiveHandleScopeSourceInfo : public ReflectionSourceInfo {
 public:
  explicit ReflectiveHandleScopeSourceInfo(BaseReflectiveHandleScope* source)
      : ReflectionSourceInfo(kSourceThreadHandleScope), source_(source) {}

  void Describe(std::ostream& os) const override;

 private:
  BaseReflectiveHandleScope* source_;
};

// TODO Maybe give this the ability to retrieve the type and ref, if it's useful.
class HeapReflectiveSourceInfo : public ReflectionSourceInfo {
 public:
  HeapReflectiveSourceInfo(ReflectionSourceType t, mirror::Object* src)
      : ReflectionSourceInfo(t), src_(src) {}
  void Describe(std::ostream& os) const override;

 private:
  ObjPtr<mirror::Object> src_;
};

// TODO Maybe give this the ability to retrieve the id if it's useful.
template <typename T,
          typename = typename std::enable_if_t<std::is_same_v<T, jmethodID> ||
                                               std::is_same_v<T, jfieldID>>>
class JniIdReflectiveSourceInfo : public ReflectionSourceInfo {
 public:
  explicit JniIdReflectiveSourceInfo(T id)
      : ReflectionSourceInfo(std::is_same_v<T, jmethodID> ? kSourceJniMethodId : kSourceJniFieldId),
        id_(id) {}
  void Describe(std::ostream& os) const override;

 private:
  T id_;
};

class DexCacheSourceInfo : public ReflectionSourceInfo {
 public:
  explicit DexCacheSourceInfo(ReflectionSourceType type,
                              size_t index,
                              ObjPtr<mirror::DexCache> cache)
      : ReflectionSourceInfo(type), index_(index), cache_(cache) {}

  void Describe(std::ostream& os) const override REQUIRES(Locks::mutator_lock_) {
    ReflectionSourceInfo::Describe(os);
    os << " index=" << index_ << " cache_=" << cache_.PtrUnchecked()
       << " files=" << *cache_->GetDexFile();
  }

 private:
  size_t index_;
  ObjPtr<mirror::DexCache> cache_;
};
}  // namespace art

#endif  // ART_RUNTIME_REFLECTIVE_VALUE_VISITOR_H_
