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

#include "image_test.h"

namespace art {
namespace linker {

class ImageWriteReadTest : public ImageTest {
 protected:
  void TestWriteRead(ImageHeader::StorageMode storage_mode, uint32_t max_image_block_size);
};

void ImageWriteReadTest::TestWriteRead(ImageHeader::StorageMode storage_mode,
                                       uint32_t max_image_block_size) {
  CompilationHelper helper;
  Compile(storage_mode, max_image_block_size, /*out*/ helper);
  std::vector<uint64_t> image_file_sizes;
  for (ScratchFile& image_file : helper.image_files) {
    std::unique_ptr<File> file(OS::OpenFileForReading(image_file.GetFilename().c_str()));
    ASSERT_TRUE(file.get() != nullptr);
    ImageHeader image_header;
    ASSERT_EQ(file->ReadFully(&image_header, sizeof(image_header)), true);
    ASSERT_TRUE(image_header.IsValid());
    const auto& bitmap_section = image_header.GetImageBitmapSection();
    ASSERT_GE(bitmap_section.Offset(), sizeof(image_header));
    ASSERT_NE(0U, bitmap_section.Size());

    gc::Heap* heap = Runtime::Current()->GetHeap();
    ASSERT_TRUE(heap->HaveContinuousSpaces());
    gc::space::ContinuousSpace* space = heap->GetNonMovingSpace();
    ASSERT_FALSE(space->IsImageSpace());
    ASSERT_TRUE(space != nullptr);
    ASSERT_TRUE(space->IsMallocSpace());
    image_file_sizes.push_back(file->GetLength());
  }

  // Need to delete the compiler since it has worker threads which are attached to runtime.
  compiler_driver_.reset();

  // Tear down old runtime before making a new one, clearing out misc state.

  // Remove the reservation of the memory for use to load the image.
  // Need to do this before we reset the runtime.
  UnreserveImageSpace();

  helper.extra_dex_files.clear();
  runtime_.reset();
  java_lang_dex_file_ = nullptr;

  MemMap::Init();

  RuntimeOptions options;
  options.emplace_back(GetClassPathOption("-Xbootclasspath:", GetLibCoreDexFileNames()), nullptr);
  options.emplace_back(
      GetClassPathOption("-Xbootclasspath-locations:", GetLibCoreDexLocations()), nullptr);
  std::string image("-Ximage:");
  image.append(helper.image_locations[0].GetFilename());
  options.push_back(std::make_pair(image.c_str(), static_cast<void*>(nullptr)));
  // By default the compiler this creates will not include patch information.
  options.push_back(std::make_pair("-Xnorelocate", nullptr));

  if (!Runtime::Create(options, false)) {
    LOG(FATAL) << "Failed to create runtime";
    return;
  }
  runtime_.reset(Runtime::Current());
  // Runtime::Create acquired the mutator_lock_ that is normally given away when we Runtime::Start,
  // give it away now and then switch to a more managable ScopedObjectAccess.
  Thread::Current()->TransitionFromRunnableToSuspended(kNative);
  ScopedObjectAccess soa(Thread::Current());
  ASSERT_TRUE(runtime_.get() != nullptr);
  class_linker_ = runtime_->GetClassLinker();

  gc::Heap* heap = Runtime::Current()->GetHeap();
  ASSERT_TRUE(heap->HasBootImageSpace());
  ASSERT_TRUE(heap->GetNonMovingSpace()->IsMallocSpace());

  // We loaded the runtime with an explicit image, so it must exist.
  ASSERT_EQ(heap->GetBootImageSpaces().size(), image_file_sizes.size());
  const HashSet<std::string>& image_classes = compiler_options_->GetImageClasses();
  for (size_t i = 0; i < helper.dex_file_locations.size(); ++i) {
    std::unique_ptr<const DexFile> dex(
        LoadExpectSingleDexFile(helper.dex_file_locations[i].c_str()));
    ASSERT_TRUE(dex != nullptr);
    uint64_t image_file_size = image_file_sizes[i];
    gc::space::ImageSpace* image_space = heap->GetBootImageSpaces()[i];
    ASSERT_TRUE(image_space != nullptr);
    if (storage_mode == ImageHeader::kStorageModeUncompressed) {
      // Uncompressed, image should be smaller than file.
      ASSERT_LE(image_space->GetImageHeader().GetImageSize(), image_file_size);
    } else if (image_file_size > 16 * KB) {
      // Compressed, file should be smaller than image. Not really valid for small images.
      ASSERT_LE(image_file_size, image_space->GetImageHeader().GetImageSize());
      // TODO: Actually validate the blocks, this is hard since the blocks are not copied over for
      // compressed images. Add kPageSize since image_size is rounded up to this.
      ASSERT_GT(image_space->GetImageHeader().GetBlockCount() * max_image_block_size,
                image_space->GetImageHeader().GetImageSize() - kPageSize);
    }

    image_space->VerifyImageAllocations();
    uint8_t* image_begin = image_space->Begin();
    uint8_t* image_end = image_space->End();
    if (i == 0) {
      // This check is only valid for image 0.
      CHECK_EQ(kRequestedImageBase, reinterpret_cast<uintptr_t>(image_begin));
    }
    for (size_t j = 0; j < dex->NumClassDefs(); ++j) {
      const dex::ClassDef& class_def = dex->GetClassDef(j);
      const char* descriptor = dex->GetClassDescriptor(class_def);
      ObjPtr<mirror::Class> klass = class_linker_->FindSystemClass(soa.Self(), descriptor);
      EXPECT_TRUE(klass != nullptr) << descriptor;
      uint8_t* raw_klass = reinterpret_cast<uint8_t*>(klass.Ptr());
      if (image_classes.find(std::string_view(descriptor)) == image_classes.end()) {
        EXPECT_TRUE(raw_klass >= image_end || raw_klass < image_begin) << descriptor;
      } else {
        // Image classes should be located inside the image.
        EXPECT_LT(image_begin, raw_klass) << descriptor;
        EXPECT_LT(raw_klass, image_end) << descriptor;
      }
      EXPECT_TRUE(Monitor::IsValidLockWord(klass->GetLockWord(false)));
    }
  }
}

TEST_F(ImageWriteReadTest, WriteReadUncompressed) {
  TestWriteRead(ImageHeader::kStorageModeUncompressed,
                /*max_image_block_size=*/std::numeric_limits<uint32_t>::max());
}

TEST_F(ImageWriteReadTest, WriteReadLZ4) {
  TestWriteRead(ImageHeader::kStorageModeLZ4,
                /*max_image_block_size=*/std::numeric_limits<uint32_t>::max());
}

TEST_F(ImageWriteReadTest, WriteReadLZ4HC) {
  TestWriteRead(ImageHeader::kStorageModeLZ4HC,
                /*max_image_block_size=*/std::numeric_limits<uint32_t>::max());
}


TEST_F(ImageWriteReadTest, WriteReadLZ4HCKBBlock) {
  TestWriteRead(ImageHeader::kStorageModeLZ4HC, /*max_image_block_size=*/KB);
}

}  // namespace linker
}  // namespace art
