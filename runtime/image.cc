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

#include "image.h"

#include <lz4.h>
#include <sstream>

#include "base/bit_utils.h"
#include "base/length_prefixed_array.h"
#include "base/utils.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/object_array.h"

namespace art {

const uint8_t ImageHeader::kImageMagic[] = { 'a', 'r', 't', '\n' };
const uint8_t ImageHeader::kImageVersion[] = { '0', '8', '5', '\0' };  // Single-image.

ImageHeader::ImageHeader(uint32_t image_reservation_size,
                         uint32_t component_count,
                         uint32_t image_begin,
                         uint32_t image_size,
                         ImageSection* sections,
                         uint32_t image_roots,
                         uint32_t oat_checksum,
                         uint32_t oat_file_begin,
                         uint32_t oat_data_begin,
                         uint32_t oat_data_end,
                         uint32_t oat_file_end,
                         uint32_t boot_image_begin,
                         uint32_t boot_image_size,
                         uint32_t boot_image_component_count,
                         uint32_t boot_image_checksum,
                         uint32_t pointer_size)
  : image_reservation_size_(image_reservation_size),
    component_count_(component_count),
    image_begin_(image_begin),
    image_size_(image_size),
    image_checksum_(0u),
    oat_checksum_(oat_checksum),
    oat_file_begin_(oat_file_begin),
    oat_data_begin_(oat_data_begin),
    oat_data_end_(oat_data_end),
    oat_file_end_(oat_file_end),
    boot_image_begin_(boot_image_begin),
    boot_image_size_(boot_image_size),
    boot_image_component_count_(boot_image_component_count),
    boot_image_checksum_(boot_image_checksum),
    image_roots_(image_roots),
    pointer_size_(pointer_size) {
  CHECK_EQ(image_begin, RoundUp(image_begin, kPageSize));
  CHECK_EQ(oat_file_begin, RoundUp(oat_file_begin, kPageSize));
  CHECK_EQ(oat_data_begin, RoundUp(oat_data_begin, kPageSize));
  CHECK_LT(image_roots, oat_file_begin);
  CHECK_LE(oat_file_begin, oat_data_begin);
  CHECK_LT(oat_data_begin, oat_data_end);
  CHECK_LE(oat_data_end, oat_file_end);
  CHECK(ValidPointerSize(pointer_size_)) << pointer_size_;
  memcpy(magic_, kImageMagic, sizeof(kImageMagic));
  memcpy(version_, kImageVersion, sizeof(kImageVersion));
  std::copy_n(sections, kSectionCount, sections_);
}

void ImageHeader::RelocateImageReferences(int64_t delta) {
  CHECK_ALIGNED(delta, kPageSize) << "relocation delta must be page aligned";
  oat_file_begin_ += delta;
  oat_data_begin_ += delta;
  oat_data_end_ += delta;
  oat_file_end_ += delta;
  image_begin_ += delta;
  image_roots_ += delta;
}

void ImageHeader::RelocateBootImageReferences(int64_t delta) {
  CHECK_ALIGNED(delta, kPageSize) << "relocation delta must be page aligned";
  DCHECK_EQ(boot_image_begin_ != 0u, boot_image_size_ != 0u);
  if (boot_image_begin_ != 0u) {
    boot_image_begin_ += delta;
  }
  for (size_t i = 0; i < kImageMethodsCount; ++i) {
    image_methods_[i] += delta;
  }
}

bool ImageHeader::IsAppImage() const {
  // Unlike boot image and boot image extensions which include address space for
  // oat files in their reservation size, app images are loaded separately from oat
  // files and their reservation size is the image size rounded up to full page.
  return image_reservation_size_ == RoundUp(image_size_, kPageSize);
}

uint32_t ImageHeader::GetImageSpaceCount() const {
  DCHECK(!IsAppImage());
  DCHECK_NE(component_count_, 0u);  // Must be the header for the first component.
  // For images compiled with --single-image, there is only one oat file. To detect
  // that, check whether the reservation ends at the end of the first oat file.
  return (image_begin_ + image_reservation_size_ == oat_file_end_) ? 1u : component_count_;
}

bool ImageHeader::IsValid() const {
  if (memcmp(magic_, kImageMagic, sizeof(kImageMagic)) != 0) {
    return false;
  }
  if (memcmp(version_, kImageVersion, sizeof(kImageVersion)) != 0) {
    return false;
  }
  if (!IsAligned<kPageSize>(image_reservation_size_)) {
    return false;
  }
  // Unsigned so wraparound is well defined.
  if (image_begin_ >= image_begin_ + image_size_) {
    return false;
  }
  if (oat_file_begin_ > oat_file_end_) {
    return false;
  }
  if (oat_data_begin_ > oat_data_end_) {
    return false;
  }
  if (oat_file_begin_ >= oat_data_begin_) {
    return false;
  }
  return true;
}

const char* ImageHeader::GetMagic() const {
  CHECK(IsValid());
  return reinterpret_cast<const char*>(magic_);
}

ArtMethod* ImageHeader::GetImageMethod(ImageMethod index) const {
  CHECK_LT(static_cast<size_t>(index), kImageMethodsCount);
  return reinterpret_cast<ArtMethod*>(image_methods_[index]);
}

std::ostream& operator<<(std::ostream& os, const ImageSection& section) {
  return os << "size=" << section.Size() << " range=" << section.Offset() << "-" << section.End();
}

void ImageHeader::VisitObjects(ObjectVisitor* visitor,
                               uint8_t* base,
                               PointerSize pointer_size) const {
  DCHECK_EQ(pointer_size, GetPointerSize());
  const ImageSection& objects = GetObjectsSection();
  static const size_t kStartPos = RoundUp(sizeof(ImageHeader), kObjectAlignment);
  for (size_t pos = kStartPos; pos < objects.Size(); ) {
    mirror::Object* object = reinterpret_cast<mirror::Object*>(base + objects.Offset() + pos);
    visitor->Visit(object);
    pos += RoundUp(object->SizeOf(), kObjectAlignment);
  }
}

PointerSize ImageHeader::GetPointerSize() const {
  return ConvertToPointerSize(pointer_size_);
}

bool ImageHeader::Block::Decompress(uint8_t* out_ptr,
                                    const uint8_t* in_ptr,
                                    std::string* error_msg) const {
  switch (storage_mode_) {
    case kStorageModeUncompressed: {
      CHECK_EQ(image_size_, data_size_);
      memcpy(out_ptr + image_offset_, in_ptr + data_offset_, data_size_);
      break;
    }
    case kStorageModeLZ4:
    case kStorageModeLZ4HC: {
      // LZ4HC and LZ4 have same internal format, both use LZ4_decompress.
      const size_t decompressed_size = LZ4_decompress_safe(
          reinterpret_cast<const char*>(in_ptr) + data_offset_,
          reinterpret_cast<char*>(out_ptr) + image_offset_,
          data_size_,
          image_size_);
      CHECK_EQ(decompressed_size, image_size_);
      break;
    }
    default: {
      if (error_msg != nullptr) {
        *error_msg = (std::ostringstream() << "Invalid image format " << storage_mode_).str();
      }
      return false;
    }
  }
  return true;
}

}  // namespace art
