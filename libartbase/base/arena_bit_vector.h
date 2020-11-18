/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_LIBARTBASE_BASE_ARENA_BIT_VECTOR_H_
#define ART_LIBARTBASE_BASE_ARENA_BIT_VECTOR_H_

#include "arena_object.h"
#include "base/arena_allocator.h"
#include "bit_vector.h"

namespace art {

class ArenaAllocator;
class ScopedArenaAllocator;

/*
 * A BitVector implementation that uses Arena allocation.
 */
class ArenaBitVector : public BitVector, public ArenaObject<kArenaAllocGrowableBitMap> {
 public:
  template <typename Allocator>
  static ArenaBitVector* Create(Allocator* allocator,
                                uint32_t start_bits,
                                bool expandable,
                                ArenaAllocKind kind = kArenaAllocGrowableBitMap) {
    void* storage = allocator->template Alloc<ArenaBitVector>(kind);
    return new (storage) ArenaBitVector(allocator, start_bits, expandable, kind);
  }

  ArenaBitVector(ArenaAllocator* allocator,
                 uint32_t start_bits,
                 bool expandable,
                 ArenaAllocKind kind = kArenaAllocGrowableBitMap);
  ArenaBitVector(ScopedArenaAllocator* allocator,
                 uint32_t start_bits,
                 bool expandable,
                 ArenaAllocKind kind = kArenaAllocGrowableBitMap);
  ~ArenaBitVector() {}

  ArenaBitVector(ArenaBitVector&&) = default;
  ArenaBitVector(const ArenaBitVector&) = delete;
};

// A BitVectorArray implementation that uses Arena allocation. See
// BitVectorArray for more information.
// This is a helper for dealing with 2d bit-vector arrays packed into a single
// bit-vector
class ArenaBitVectorArray final : public BaseBitVectorArray,
                                  public ArenaObject<kArenaAllocGrowableBitMap> {
 public:
  ArenaBitVectorArray(const ArenaBitVectorArray& bv) = delete;
  ArenaBitVectorArray& operator=(const ArenaBitVectorArray& other) = delete;

  explicit ArenaBitVectorArray(ArenaBitVector&& bv) : BaseBitVectorArray(), data_(std::move(bv)) {}
  ArenaBitVectorArray(ArenaBitVector&& bv, size_t cols)
      : BaseBitVectorArray(BaseBitVectorArray::MaxRowsFor(bv, cols), cols), data_(std::move(bv)) {}

  ArenaBitVectorArray(ArenaAllocator* allocator,
                      size_t start_rows,
                      size_t start_cols,
                      bool expandable,
                      ArenaAllocKind kind = kArenaAllocGrowableBitMap)
      : BaseBitVectorArray(start_rows, start_cols),
        data_(ArenaBitVector(allocator,
                             BaseBitVectorArray::RequiredBitVectorSize(start_rows, start_cols),
                             expandable,
                             kind)) {}

  ArenaBitVectorArray(ScopedArenaAllocator* allocator,
                      size_t start_rows,
                      size_t start_cols,
                      bool expandable,
                      ArenaAllocKind kind = kArenaAllocGrowableBitMap)
      : BaseBitVectorArray(start_rows, start_cols),
        data_(ArenaBitVector(allocator,
                             BaseBitVectorArray::RequiredBitVectorSize(start_rows, start_cols),
                             expandable,
                             kind)) {}

  ~ArenaBitVectorArray() override {}

  const BitVector& GetRawData() const override {
    return data_;
  }

  BitVector& GetRawData() override {
    return data_;
  }

 private:
  ArenaBitVector data_;
};

}  // namespace art

#endif  // ART_LIBARTBASE_BASE_ARENA_BIT_VECTOR_H_
