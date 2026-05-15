/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "runtime_image.h"

#include <lz4.h>
#include <sys/stat.h>
#include <unistd.h>

#include "android-base/file.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "arch/instruction_set.h"
#include "arch/instruction_set_features.h"
#include "base/arena_allocator.h"
#include "base/arena_containers.h"
#include "base/bit_utils.h"
#include "base/file_utils.h"
#include "base/length_prefixed_array.h"
#include "base/scoped_flock.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "class_loader_context.h"
#include "class_loader_utils.h"
#include "class_root-inl.h"
#include "dex/class_accessor-inl.h"
#include "gc/space/image_space.h"
#include "mirror/object-inl.h"
#include "mirror/object-refvisitor-inl.h"
#include "mirror/object_array-alloc-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/object_array.h"
#include "mirror/string-inl.h"
#include "nterp_helpers.h"
#include "oat/image.h"
#include "oat/oat.h"
#include "profile/profile_compilation_info.h"
#include "scoped_thread_state_change-inl.h"
#include "vdex_file.h"

namespace art HIDDEN {

using android::base::StringPrintf;

/**
 * The native data structures that we store in the image.
 */
enum class NativeRelocationKind {
  kArtFieldArray,
  kArtMethodArray,
  kArtMethod,
  kImTable,
  // For dex cache arrays which can stay in memory even after startup. Those are
  // dex cache arrays whose size is below a given threshold, defined by
  // DexCache::ShouldAllocateFullArray.
  kFullNativeDexCacheArray,
  // For dex cache arrays which we will want to release after app startup.
  kStartupNativeDexCacheArray,
};

/**
 * Helper class to generate an app image at runtime.
 */
class RuntimeImageHelper {
 public:
  explicit RuntimeImageHelper(gc::Heap* heap) :
    allocator_(Runtime::Current()->GetArenaPool()),
    objects_(allocator_.Adapter()),
    art_fields_(allocator_.Adapter()),
    art_methods_(allocator_.Adapter()),
    im_tables_(allocator_.Adapter()),
    metadata_(allocator_.Adapter()),
    dex_cache_arrays_(allocator_.Adapter()),
    string_reference_offsets_(allocator_.Adapter()),
    sections_(ImageHeader::kSectionCount, allocator_.Adapter()),
    object_offsets_(allocator_.Adapter()),
    classes_(allocator_.Adapter()),
    array_classes_(allocator_.Adapter()),
    dex_caches_(allocator_.Adapter()),
    class_hashes_(allocator_.Adapter()),
    native_relocations_(allocator_.Adapter()),
    boot_image_begin_(heap->GetBootImagesStartAddress()),
    boot_image_size_(heap->GetBootImagesSize()),
    image_begin_(boot_image_begin_ + boot_image_size_),
    // Note: image relocation considers the image header in the bitmap.
    object_section_size_(sizeof(ImageHeader)),
    intern_table_(InternStringHash(this), InternStringEquals(this)),
    class_table_(ClassDescriptorHash(this), ClassDescriptorEquals()) {}

  bool Generate(std::string* error_msg) {
    if (!WriteObjects(error_msg)) {
      return false;
    }

    // Generate the sections information stored in the header.
    CreateImageSections();

    // Now that all sections have been created and we know their offset and
    // size, relocate native pointers inside classes and ImTables.
    RelocateNativePointers();

    // Generate the bitmap section, stored kElfSegmentAlignment-aligned after the sections data and
    // of size `object_section_size_` rounded up to kCardSize to match the bitmap size expected by
    // Loader::Init at art::gc::space::ImageSpace.
    size_t sections_end = sections_[ImageHeader::kSectionMetadata].End();
    image_bitmap_ = gc::accounting::ContinuousSpaceBitmap::Create(
        "image bitmap",
        reinterpret_cast<uint8_t*>(image_begin_),
        RoundUp(object_section_size_, gc::accounting::CardTable::kCardSize));
    for (uint32_t offset : object_offsets_) {
      DCHECK(IsAligned<kObjectAlignment>(image_begin_ + sizeof(ImageHeader) + offset));
      image_bitmap_.Set(
          reinterpret_cast<mirror::Object*>(image_begin_ + sizeof(ImageHeader) + offset));
    }
    const size_t bitmap_bytes = image_bitmap_.Size();
    auto* bitmap_section = &sections_[ImageHeader::kSectionImageBitmap];
    // The offset of the bitmap section should be aligned to kElfSegmentAlignment to enable mapping
    // the section from file to memory. However the section size doesn't have to be rounded up as
    // it is located at the end of the file. When mapping file contents to memory, if the last page
    // of the mapping is only partially filled with data, the rest will be zero-filled.
    *bitmap_section = ImageSection(RoundUp(sections_end, kElfSegmentAlignment), bitmap_bytes);

    // Compute boot image checksum and boot image components, to be stored in
    // the header.
    gc::Heap* const heap = Runtime::Current()->GetHeap();
    uint32_t boot_image_components = 0u;
    uint32_t boot_image_checksums = 0u;
    const std::vector<gc::space::ImageSpace*>& image_spaces = heap->GetBootImageSpaces();
    for (size_t i = 0u, size = image_spaces.size(); i != size; ) {
      const ImageHeader& header = image_spaces[i]->GetImageHeader();
      boot_image_components += header.GetComponentCount();
      boot_image_checksums ^= header.GetImageChecksum();
      DCHECK_LE(header.GetImageSpaceCount(), size - i);
      i += header.GetImageSpaceCount();
    }

    header_ = ImageHeader(
        /* image_reservation_size= */ RoundUp(sections_end, kElfSegmentAlignment),
        /* component_count= */ 1,
        image_begin_,
        sections_end,
        sections_.data(),
        /* image_roots= */ image_begin_ + sizeof(ImageHeader),
        /* oat_checksum= */ 0,
        /* oat_file_begin= */ 0,
        /* oat_data_begin= */ 0,
        /* oat_data_end= */ 0,
        /* oat_file_end= */ 0,
        heap->GetBootImagesStartAddress(),
        heap->GetBootImagesSize(),
        boot_image_components,
        boot_image_checksums,
        static_cast<uint32_t>(kRuntimePointerSize));

    // Data size includes everything except the bitmap and the header.
    header_.data_size_ = sections_end - sizeof(ImageHeader);

    // Write image methods - needs to happen after creation of the header.
    WriteImageMethods();

    return true;
  }

  void FillData(std::vector<uint8_t>& data) {
    // Note we don't put the header, we only have it reserved in `data` as
    // Image::WriteData expects the object section to contain the image header.
    auto compute_dest = [&](const ImageSection& section) {
      return data.data() + section.Offset();
    };

    auto objects_section = header_.GetImageSection(ImageHeader::kSectionObjects);
    memcpy(compute_dest(objects_section) + sizeof(ImageHeader), objects_.data(), objects_.size());

    auto fields_section = header_.GetImageSection(ImageHeader::kSectionArtFields);
    memcpy(compute_dest(fields_section), art_fields_.data(), fields_section.Size());

    auto methods_section = header_.GetImageSection(ImageHeader::kSectionArtMethods);
    memcpy(compute_dest(methods_section), art_methods_.data(), methods_section.Size());

    auto im_tables_section = header_.GetImageSection(ImageHeader::kSectionImTables);
    memcpy(compute_dest(im_tables_section), im_tables_.data(), im_tables_section.Size());

    auto intern_section = header_.GetImageSection(ImageHeader::kSectionInternedStrings);
    intern_table_.WriteToMemory(compute_dest(intern_section));

    auto class_table_section = header_.GetImageSection(ImageHeader::kSectionClassTable);
    class_table_.WriteToMemory(compute_dest(class_table_section));

    auto string_offsets_section =
        header_.GetImageSection(ImageHeader::kSectionStringReferenceOffsets);
    memcpy(compute_dest(string_offsets_section),
           string_reference_offsets_.data(),
           string_offsets_section.Size());

    auto dex_cache_section = header_.GetImageSection(ImageHeader::kSectionDexCacheArrays);
    memcpy(compute_dest(dex_cache_section), dex_cache_arrays_.data(), dex_cache_section.Size());

    auto metadata_section = header_.GetImageSection(ImageHeader::kSectionMetadata);
    memcpy(compute_dest(metadata_section), metadata_.data(), metadata_section.Size());

    DCHECK_EQ(metadata_section.Offset() + metadata_section.Size(), data.size());
  }


  ImageHeader* GetHeader() {
    return &header_;
  }

  const gc::accounting::ContinuousSpaceBitmap& GetImageBitmap() const {
    return image_bitmap_;
  }

  const std::string& GetDexLocation() const {
    return dex_location_;
  }

 private:
  bool IsInBootImage(const void* obj) const {
    return reinterpret_cast<uintptr_t>(obj) - boot_image_begin_ < boot_image_size_;
  }

  // Returns the image contents for `cls`. If `cls` is in the boot image, the
  // method just returns it.
  mirror::Class* GetClassContent(ObjPtr<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (cls == nullptr || IsInBootImage(cls.Ptr())) {
      return cls.Ptr();
    }
    const dex::ClassDef* class_def = cls->GetClassDef();
    DCHECK(class_def != nullptr) << cls->PrettyClass();
    auto it = classes_.find(class_def);
    DCHECK(it != classes_.end()) << cls->PrettyClass();
    mirror::Class* result = reinterpret_cast<mirror::Class*>(objects_.data() + it->second);
    DCHECK(result->GetClass()->IsClass());
    return result;
  }

  // Returns a pointer that can be stored in `objects_`:
  // - The pointer itself for boot image objects,
  // - The offset in the image for all other objects.
  template <typename T> T* GetOrComputeImageAddress(ObjPtr<T> object)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (object == nullptr || IsInBootImage(object.Ptr())) {
      DCHECK(object == nullptr || Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(object));
      return object.Ptr();
    }

    if (object->IsClassLoader()) {
      // DexCache and Class point to class loaders. For runtime-generated app
      // images, we don't encode the class loader. It will be set when the
      // runtime is loading the image.
      return nullptr;
    }

    if (object->GetClass() == GetClassRoot<mirror::ClassExt>()) {
      // No need to encode `ClassExt`. If needed, it will be reconstructed at
      // runtime.
      return nullptr;
    }

    uint32_t offset = 0u;
    if (object->IsClass()) {
      offset = CopyClass(object->AsClass());
    } else if (object->IsDexCache()) {
      offset = CopyDexCache(object->AsDexCache());
    } else {
      offset = CopyObject(object);
    }
    return reinterpret_cast<T*>(image_begin_ + sizeof(ImageHeader) + offset);
  }

  void CreateImageSections() {
    sections_[ImageHeader::kSectionObjects] = ImageSection(0u, object_section_size_);
    sections_[ImageHeader::kSectionArtFields] =
        ImageSection(sections_[ImageHeader::kSectionObjects].End(), art_fields_.size());

    // Round up to the alignment for ArtMethod.
    static_assert(IsAligned<sizeof(void*)>(ArtMethod::Size(kRuntimePointerSize)));
    size_t cur_pos = RoundUp(sections_[ImageHeader::kSectionArtFields].End(), sizeof(void*));
    sections_[ImageHeader::kSectionArtMethods] = ImageSection(cur_pos, art_methods_.size());

    // Round up to the alignment for ImTables.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionArtMethods].End(), sizeof(void*));
    sections_[ImageHeader::kSectionImTables] = ImageSection(cur_pos, im_tables_.size());

    // Round up to the alignment for conflict tables.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionImTables].End(), sizeof(void*));
    sections_[ImageHeader::kSectionIMTConflictTables] = ImageSection(cur_pos, 0u);

    sections_[ImageHeader::kSectionRuntimeMethods] =
        ImageSection(sections_[ImageHeader::kSectionIMTConflictTables].End(), 0u);

    // Round up to the alignment the string table expects. See HashSet::WriteToMemory.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionRuntimeMethods].End(), sizeof(uint64_t));

    size_t intern_table_bytes = intern_table_.WriteToMemory(nullptr);
    sections_[ImageHeader::kSectionInternedStrings] = ImageSection(cur_pos, intern_table_bytes);

    // Obtain the new position and round it up to the appropriate alignment.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionInternedStrings].End(), sizeof(uint64_t));

    size_t class_table_bytes = class_table_.WriteToMemory(nullptr);
    sections_[ImageHeader::kSectionClassTable] = ImageSection(cur_pos, class_table_bytes);

    // Round up to the alignment of the offsets we are going to store.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionClassTable].End(), sizeof(uint32_t));
    sections_[ImageHeader::kSectionStringReferenceOffsets] = ImageSection(
        cur_pos, string_reference_offsets_.size() * sizeof(string_reference_offsets_[0]));

    // Round up to the alignment dex caches arrays expects.
    cur_pos =
        RoundUp(sections_[ImageHeader::kSectionStringReferenceOffsets].End(), sizeof(void*));
    sections_[ImageHeader::kSectionDexCacheArrays] =
        ImageSection(cur_pos, dex_cache_arrays_.size());

    // Round up to the alignment expected for the metadata, which holds dex
    // cache arrays.
    cur_pos = RoundUp(sections_[ImageHeader::kSectionDexCacheArrays].End(), sizeof(void*));
    sections_[ImageHeader::kSectionMetadata] = ImageSection(cur_pos, metadata_.size());
  }

  // Returns the copied mirror Object if in the image, or the object directly if
  // in the boot image. For the copy, this is really its content, it should not
  // be returned as an `ObjPtr` (as it's not a GC object), nor stored anywhere.
  template<typename T> T* FromImageOffsetToRuntimeContent(uint32_t offset) {
    if (offset == 0u || IsInBootImage(reinterpret_cast<const void*>(offset))) {
      return reinterpret_cast<T*>(offset);
    }
    uint32_t vector_data_offset = FromImageOffsetToVectorOffset(offset);
    return reinterpret_cast<T*>(objects_.data() + vector_data_offset);
  }

  uint32_t FromImageOffsetToVectorOffset(uint32_t offset) const {
    DCHECK(!IsInBootImage(reinterpret_cast<const void*>(offset)));
    return offset - sizeof(ImageHeader) - image_begin_;
  }

  class InternStringHash {
   public:
    explicit InternStringHash(RuntimeImageHelper* helper) : helper_(helper) {}

    // NO_THREAD_SAFETY_ANALYSIS as these helpers get passed to `HashSet`.
    size_t operator()(mirror::String* str) const NO_THREAD_SAFETY_ANALYSIS {
      int32_t hash = str->GetStoredHashCode();
      DCHECK_EQ(hash, str->ComputeHashCode());
      // An additional cast to prevent undesired sign extension.
      return static_cast<uint32_t>(hash);
    }

    size_t operator()(uint32_t entry) const NO_THREAD_SAFETY_ANALYSIS {
      return (*this)(helper_->FromImageOffsetToRuntimeContent<mirror::String>(entry));
    }

   private:
    RuntimeImageHelper* helper_;
  };

  class InternStringEquals {
   public:
    explicit InternStringEquals(RuntimeImageHelper* helper) : helper_(helper) {}

    // NO_THREAD_SAFETY_ANALYSIS as these helpers get passed to `HashSet`.
    bool operator()(uint32_t entry, mirror::String* other) const NO_THREAD_SAFETY_ANALYSIS {
      if (kIsDebugBuild) {
        Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
      }
      return other->Equals(helper_->FromImageOffsetToRuntimeContent<mirror::String>(entry));
    }

    bool operator()(uint32_t entry, uint32_t other) const NO_THREAD_SAFETY_ANALYSIS {
      return (*this)(entry, helper_->FromImageOffsetToRuntimeContent<mirror::String>(other));
    }

   private:
    RuntimeImageHelper* helper_;
  };

  using InternTableSet =
        HashSet<uint32_t, DefaultEmptyFn<uint32_t>, InternStringHash, InternStringEquals>;

  class ClassDescriptorHash {
   public:
    explicit ClassDescriptorHash(RuntimeImageHelper* helper) : helper_(helper) {}

    uint32_t operator()(const ClassTable::TableSlot& slot) const NO_THREAD_SAFETY_ANALYSIS {
      uint32_t ptr = slot.NonHashData();
      if (helper_->IsInBootImage(reinterpret_cast32<const void*>(ptr))) {
        return reinterpret_cast32<mirror::Class*>(ptr)->DescriptorHash();
      }
      return helper_->class_hashes_.Get(helper_->FromImageOffsetToVectorOffset(ptr));
    }

   private:
    RuntimeImageHelper* helper_;
  };

  class ClassDescriptorEquals {
   public:
    ClassDescriptorEquals() {}

    bool operator()(const ClassTable::TableSlot& a, const ClassTable::TableSlot& b)
        const NO_THREAD_SAFETY_ANALYSIS {
      // No need to fetch the descriptor: we know the classes we are inserting
      // in the ClassTable are unique.
      return a.Data() == b.Data();
    }
  };

  using ClassTableSet = HashSet<ClassTable::TableSlot,
                                ClassTable::TableSlotEmptyFn,
                                ClassDescriptorHash,
                                ClassDescriptorEquals>;

  // Helper class to collect classes that we will generate in the image.
  class ClassTableVisitor {
   public:
    ClassTableVisitor(Handle<mirror::ClassLoader> loader, VariableSizedHandleScope& handles)
        : loader_(loader), handles_(handles) {}

    bool operator()(ObjPtr<mirror::Class> klass) REQUIRES_SHARED(Locks::mutator_lock_) {
      // Record app classes and boot classpath classes: app classes will be
      // generated in the image and put in the class table, boot classpath
      // classes will be put in the class table.
      ObjPtr<mirror::ClassLoader> class_loader = klass->GetClassLoader();
      if (klass->IsResolved() && (class_loader == loader_.Get() || class_loader == nullptr)) {
        handles_.NewHandle(klass);
      }
      return true;
    }

   private:
    Handle<mirror::ClassLoader> loader_;
    VariableSizedHandleScope& handles_;
  };

  // Helper class visitor to filter out classes we cannot emit.
  class PruneVisitor {
   public:
    PruneVisitor(Thread* self,
                 RuntimeImageHelper* helper,
                 const ArenaSet<const DexFile*>& dex_files,
                 ArenaVector<Handle<mirror::Class>>& classes,
                 ArenaAllocator& allocator)
        : self_(self),
          helper_(helper),
          dex_files_(dex_files),
          visited_(allocator.Adapter()),
          classes_to_write_(classes) {}

    bool CanEmitHelper(Handle<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
      // If the class comes from a dex file which is not part of the primary
      // APK, don't encode it.
      if (!ContainsElement(dex_files_, &cls->GetDexFile())) {
        return false;
      }

      // Ensure pointers to classes in `cls` can also be emitted.
      StackHandleScope<1> hs(self_);
      MutableHandle<mirror::Class> other_class = hs.NewHandle(cls->GetSuperClass());
      if (!CanEmit(other_class)) {
        return false;
      }

      other_class.Assign(cls->GetComponentType());
      if (!CanEmit(other_class)) {
        return false;
      }

      for (size_t i = 0, num_interfaces = cls->NumDirectInterfaces(); i < num_interfaces; ++i) {
        other_class.Assign(cls->GetDirectInterface(i));
        DCHECK(other_class != nullptr);
        if (!CanEmit(other_class)) {
          return false;
        }
      }
      return true;
    }

    bool CanEmit(Handle<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
      if (cls == nullptr) {
        return true;
      }
      DCHECK(cls->IsResolved());
      // Only emit classes that are resolved and not erroneous.
      if (cls->IsErroneous()) {
        return false;
      }

      // Proxy classes are generated at runtime, so don't emit them.
      if (cls->IsProxyClass()) {
        return false;
      }

      // Classes in the boot image can be trivially encoded directly.
      if (helper_->IsInBootImage(cls.Get())) {
        return true;
      }

      if (cls->IsBootStrapClassLoaded()) {
        // We cannot encode classes that are part of the boot classpath.
        return false;
      }

      DCHECK(!cls->IsPrimitive());

      if (cls->IsArrayClass()) {
        if (cls->IsBootStrapClassLoaded()) {
          // For boot classpath arrays, we can only emit them if they are
          // in the boot image already.
          return helper_->IsInBootImage(cls.Get());
        }
        ObjPtr<mirror::Class> temp = cls.Get();
        while ((temp = temp->GetComponentType())->IsArrayClass()) {}
        StackHandleScope<1> hs(self_);
        Handle<mirror::Class> other_class = hs.NewHandle(temp);
        return CanEmit(other_class);
      }
      const dex::ClassDef* class_def = cls->GetClassDef();
      DCHECK_NE(class_def, nullptr);
      auto existing = visited_.find(class_def);
      if (existing != visited_.end()) {
        // Already processed;
        return existing->second == VisitState::kCanEmit;
      }

      visited_.Put(class_def, VisitState::kVisiting);
      if (CanEmitHelper(cls)) {
        visited_.Overwrite(class_def, VisitState::kCanEmit);
        return true;
      } else {
        visited_.Overwrite(class_def, VisitState::kCannotEmit);
        return false;
      }
    }

    void Visit(Handle<mirror::Object> obj) REQUIRES_SHARED(Locks::mutator_lock_) {
      MutableHandle<mirror::Class> cls(obj.GetReference());
      if (CanEmit(cls)) {
        if (cls->IsBootStrapClassLoaded()) {
          DCHECK(helper_->IsInBootImage(cls.Get()));
          // Insert the bootclasspath class in the class table.
          uint32_t hash = cls->DescriptorHash();
          helper_->class_table_.InsertWithHash(ClassTable::TableSlot(cls.Get(), hash), hash);
        } else {
          classes_to_write_.push_back(cls);
        }
      }
    }

   private:
    enum class VisitState {
      kVisiting,
      kCanEmit,
      kCannotEmit,
    };

    Thread* const self_;
    RuntimeImageHelper* const helper_;
    const ArenaSet<const DexFile*>& dex_files_;
    ArenaSafeMap<const dex::ClassDef*, VisitState> visited_;
    ArenaVector<Handle<mirror::Class>>& classes_to_write_;
  };

  void EmitClasses(Thread* self, Handle<mirror::ObjectArray<mirror::Object>> dex_cache_array)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    ScopedTrace trace("Emit strings and classes");
    ArenaSet<const DexFile*> dex_files(allocator_.Adapter());
    for (int32_t i = 0; i < dex_cache_array->GetLength(); ++i) {
      dex_files.insert(dex_cache_array->Get(i)->AsDexCache()->GetDexFile());
    }

    StackHandleScope<1> hs(self);
    Handle<mirror::ClassLoader> loader = hs.NewHandle(
        dex_cache_array->Get(0)->AsDexCache()->GetClassLoader());
    ClassTable* const class_table = loader->GetClassTable();
    if (class_table == nullptr) {
      return;
    }

    VariableSizedHandleScope handles(self);
    {
      ClassTableVisitor class_table_visitor(loader, handles);
      class_table->Visit(class_table_visitor);
    }

    ArenaVector<Handle<mirror::Class>> classes_to_write(allocator_.Adapter());
    classes_to_write.reserve(class_table->Size());
    {
      PruneVisitor prune_visitor(self, this, dex_files, classes_to_write, allocator_);
      handles.VisitHandles(prune_visitor);
    }

    for (Handle<mirror::Class> cls : classes_to_write) {
      {
        ScopedAssertNoThreadSuspension sants("Writing class");
        CopyClass(cls.Get());
      }
      self->AllowThreadSuspension();
    }

    // Relocate the type array entries. We do this now before creating image
    // sections because we may add new boot image classes into our
    // `class_table`_.
    for (auto entry : dex_caches_) {
      const DexFile& dex_file = *entry.first;
      mirror::DexCache* cache = reinterpret_cast<mirror::DexCache*>(&objects_[entry.second]);
      mirror::GcRootArray<mirror::Class>* old_types_array = cache->GetResolvedTypesArray();
      if (HasNativeRelocation(old_types_array)) {
        auto reloc_it = native_relocations_.find(old_types_array);
        DCHECK(reloc_it != native_relocations_.end());
        ArenaVector<uint8_t>& data =
            (reloc_it->second.first == NativeRelocationKind::kFullNativeDexCacheArray)
                ? dex_cache_arrays_ : metadata_;
        mirror::GcRootArray<mirror::Class>* content_array =
            reinterpret_cast<mirror::GcRootArray<mirror::Class>*>(
                data.data() + reloc_it->second.second);
        for (uint32_t i = 0; i < dex_file.NumTypeIds(); ++i) {
          ObjPtr<mirror::Class> cls = old_types_array->Get(i);
          if (cls == nullptr) {
            content_array->Set(i, nullptr);
          } else if (IsInBootImage(cls.Ptr())) {
            if (!cls->IsPrimitive()) {
              // The dex cache is concurrently updated by the app. If the class
              // collection logic in `PruneVisitor` did not see this class, insert it now.
              // Note that application class tables do not contain primitive
              // classes.
              uint32_t hash = cls->DescriptorHash();
              class_table_.InsertWithHash(ClassTable::TableSlot(cls.Ptr(), hash), hash);
            }
            content_array->Set(i, cls.Ptr());
          } else if (cls->IsArrayClass()) {
            std::string class_name;
            cls->GetDescriptor(&class_name);
            auto class_it = array_classes_.find(class_name);
            if (class_it == array_classes_.end()) {
              content_array->Set(i, nullptr);
            } else {
              mirror::Class* ptr = reinterpret_cast<mirror::Class*>(
                  image_begin_ + sizeof(ImageHeader) + class_it->second);
              content_array->Set(i, ptr);
            }
          } else {
            DCHECK(!cls->IsPrimitive());
            DCHECK(!cls->IsProxyClass());
            const dex::ClassDef* class_def = cls->GetClassDef();
            DCHECK_NE(class_def, nullptr);
            auto class_it = classes_.find(class_def);
            if (class_it == classes_.end()) {
              content_array->Set(i, nullptr);
            } else {
              mirror::Class* ptr = reinterpret_cast<mirror::Class*>(
                  image_begin_ + sizeof(ImageHeader) + class_it->second);
              content_array->Set(i, ptr);
            }
          }
        }
      }
    }
  }

  // Helper visitor returning the location of a native pointer in the image.
  class NativePointerVisitor {
   public:
    explicit NativePointerVisitor(RuntimeImageHelper* helper) : helper_(helper) {}

    template <typename T>
    T* operator()(T* ptr, [[maybe_unused]] void** dest_addr) const {
      return helper_->NativeLocationInImage(ptr, /* must_have_relocation= */ true);
    }

    template <typename T> T* operator()(T* ptr, bool must_have_relocation = true) const {
      return helper_->NativeLocationInImage(ptr, must_have_relocation);
    }

   private:
    RuntimeImageHelper* helper_;
  };

  template <typename T> T* NativeLocationInImage(T* ptr, bool must_have_relocation) const {
    if (ptr == nullptr || IsInBootImage(ptr)) {
      return ptr;
    }

    auto it = native_relocations_.find(ptr);
    if (it == native_relocations_.end()) {
      DCHECK(!must_have_relocation);
      return nullptr;
    }
    switch (it->second.first) {
      case NativeRelocationKind::kArtMethod:
      case NativeRelocationKind::kArtMethodArray: {
        uint32_t offset = sections_[ImageHeader::kSectionArtMethods].Offset();
        return reinterpret_cast<T*>(image_begin_ + offset + it->second.second);
      }
      case NativeRelocationKind::kArtFieldArray: {
        uint32_t offset = sections_[ImageHeader::kSectionArtFields].Offset();
        return reinterpret_cast<T*>(image_begin_ + offset + it->second.second);
      }
      case NativeRelocationKind::kImTable: {
        uint32_t offset = sections_[ImageHeader::kSectionImTables].Offset();
        return reinterpret_cast<T*>(image_begin_ + offset + it->second.second);
      }
      case NativeRelocationKind::kStartupNativeDexCacheArray: {
        uint32_t offset = sections_[ImageHeader::kSectionMetadata].Offset();
        return reinterpret_cast<T*>(image_begin_ + offset + it->second.second);
      }
      case NativeRelocationKind::kFullNativeDexCacheArray: {
        uint32_t offset = sections_[ImageHeader::kSectionDexCacheArrays].Offset();
        return reinterpret_cast<T*>(image_begin_ + offset + it->second.second);
      }
    }
  }

  template <typename Visitor>
  void RelocateMethodPointerArrays(mirror::Class* klass, const Visitor& visitor)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    // A bit of magic here: we cast contents from our buffer to mirror::Class,
    // and do pointer comparison between 1) these classes, and 2) boot image objects.
    // Both kinds do not move.

    // See if we need to fixup the vtable field.
    mirror::Class* super = FromImageOffsetToRuntimeContent<mirror::Class>(
        reinterpret_cast32<uint32_t>(
            klass->GetSuperClass<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    DCHECK(super != nullptr) << "j.l.Object should never be in an app runtime image";
    mirror::PointerArray* vtable = FromImageOffsetToRuntimeContent<mirror::PointerArray>(
        reinterpret_cast32<uint32_t>(klass->GetVTable<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    mirror::PointerArray* super_vtable = FromImageOffsetToRuntimeContent<mirror::PointerArray>(
        reinterpret_cast32<uint32_t>(super->GetVTable<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    if (vtable != nullptr && vtable != super_vtable) {
      DCHECK(!IsInBootImage(vtable));
      vtable->Fixup(vtable, kRuntimePointerSize, visitor);
    }

    // See if we need to fixup entries in the IfTable.
    mirror::IfTable* iftable = FromImageOffsetToRuntimeContent<mirror::IfTable>(
        reinterpret_cast32<uint32_t>(
            klass->GetIfTable<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    mirror::IfTable* super_iftable = FromImageOffsetToRuntimeContent<mirror::IfTable>(
        reinterpret_cast32<uint32_t>(
            super->GetIfTable<kVerifyNone, kWithoutReadBarrier>().Ptr()));
    int32_t iftable_count = iftable->Count();
    int32_t super_iftable_count = super_iftable->Count();
    for (int32_t i = 0; i < iftable_count; ++i) {
      mirror::PointerArray* methods = FromImageOffsetToRuntimeContent<mirror::PointerArray>(
          reinterpret_cast32<uint32_t>(
              iftable->GetMethodArrayOrNull<kVerifyNone, kWithoutReadBarrier>(i).Ptr()));
      mirror::PointerArray* super_methods = (i < super_iftable_count)
          ? FromImageOffsetToRuntimeContent<mirror::PointerArray>(
                reinterpret_cast32<uint32_t>(
                    super_iftable->GetMethodArrayOrNull<kVerifyNone, kWithoutReadBarrier>(i).Ptr()))
          : nullptr;
      if (methods != super_methods) {
        DCHECK(!IsInBootImage(methods));
        methods->Fixup(methods, kRuntimePointerSize, visitor);
      }
    }
  }

  template <typename Visitor, typename T>
  void RelocateNativeDexCacheArray(mirror::NativeArray<T>* old_method_array,
                                   uint32_t num_ids,
                                   const Visitor& visitor)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (old_method_array == nullptr) {
      return;
    }

    auto it = native_relocations_.find(old_method_array);
    DCHECK(it != native_relocations_.end());
    ArenaVector<uint8_t>& data =
        (it->second.first == NativeRelocationKind::kFullNativeDexCacheArray)
            ? dex_cache_arrays_ : metadata_;

    mirror::NativeArray<T>* content_array =
        reinterpret_cast<mirror::NativeArray<T>*>(data.data() + it->second.second);
    for (uint32_t i = 0; i < num_ids; ++i) {
      // We may not have relocations for some entries, in which case we'll
      // just store null.
      content_array->Set(i, visitor(content_array->Get(i), /* must_have_relocation= */ false));
    }
  }

  template <typename Visitor>
  void RelocateDexCacheArrays(mirror::DexCache* cache,
                              const DexFile& dex_file,
                              const Visitor& visitor)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    mirror::NativeArray<ArtMethod>* old_method_array = cache->GetResolvedMethodsArray();
    cache->SetResolvedMethodsArray(visitor(old_method_array));
    RelocateNativeDexCacheArray(old_method_array, dex_file.NumMethodIds(), visitor);

    mirror::NativeArray<ArtField>* old_field_array = cache->GetResolvedFieldsArray();
    cache->SetResolvedFieldsArray(visitor(old_field_array));
    RelocateNativeDexCacheArray(old_field_array, dex_file.NumFieldIds(), visitor);

    mirror::GcRootArray<mirror::String>* old_strings_array = cache->GetStringsArray();
    cache->SetStringsArray(visitor(old_strings_array));

    mirror::GcRootArray<mirror::Class>* old_types_array = cache->GetResolvedTypesArray();
    cache->SetResolvedTypesArray(visitor(old_types_array));
  }

  void RelocateNativePointers() {
    ScopedTrace relocate_native_pointers("Relocate native pointers");
    ScopedObjectAccess soa(Thread::Current());
    NativePointerVisitor visitor(this);
    for (auto&& entry : classes_) {
      mirror::Class* cls = reinterpret_cast<mirror::Class*>(&objects_[entry.second]);
      cls->FixupNativePointers(cls, kRuntimePointerSize, visitor);
      RelocateMethodPointerArrays(cls, visitor);
    }
    for (auto&& entry : array_classes_) {
      mirror::Class* cls = reinterpret_cast<mirror::Class*>(&objects_[entry.second]);
      cls->FixupNativePointers(cls, kRuntimePointerSize, visitor);
      RelocateMethodPointerArrays(cls, visitor);
    }
    for (auto&& entry : native_relocations_) {
      if (entry.second.first == NativeRelocationKind::kImTable) {
        ImTable* im_table = reinterpret_cast<ImTable*>(im_tables_.data() + entry.second.second);
        RelocateImTable(im_table, visitor);
      }
    }
    for (auto&& entry : dex_caches_) {
      mirror::DexCache* cache = reinterpret_cast<mirror::DexCache*>(&objects_[entry.second]);
      RelocateDexCacheArrays(cache, *entry.first, visitor);
    }
  }

  void RelocateImTable(ImTable* im_table, const NativePointerVisitor& visitor) {
    for (size_t i = 0; i < ImTable::kSize; ++i) {
      ArtMethod* method = im_table->Get(i, kRuntimePointerSize);
      ArtMethod* new_method = nullptr;
      if (method->IsRuntimeMethod() && !IsInBootImage(method)) {
        // New IMT conflict method: just use the boot image version.
        // TODO: Consider copying the new IMT conflict method.
        new_method = Runtime::Current()->GetImtConflictMethod();
        DCHECK(IsInBootImage(new_method));
      } else {
        new_method = visitor(method);
      }
      if (method != new_method) {
        im_table->Set(i, new_method, kRuntimePointerSize);
      }
    }
  }

  void CopyFieldArrays(ObjPtr<mirror::Class> cls, uint32_t class_image_address)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    LengthPrefixedArray<ArtField>* fields[] = {
        cls->GetSFieldsPtr(), cls->GetIFieldsPtr(),
    };
    for (LengthPrefixedArray<ArtField>* cur_fields : fields) {
      if (cur_fields != nullptr) {
        // Copy the array.
        size_t number_of_fields = cur_fields->size();
        size_t size = LengthPrefixedArray<ArtField>::ComputeSize(number_of_fields);
        size_t offset = art_fields_.size();
        art_fields_.resize(offset + size);
        auto* dest_array =
            reinterpret_cast<LengthPrefixedArray<ArtField>*>(art_fields_.data() + offset);
        memcpy(dest_array, cur_fields, size);
        native_relocations_.Put(cur_fields,
                                std::make_pair(NativeRelocationKind::kArtFieldArray, offset));

        // Update the class pointer of individual fields.
        for (size_t i = 0; i != number_of_fields; ++i) {
          dest_array->At(i).GetDeclaringClassAddressWithoutBarrier()->Assign(
              reinterpret_cast<mirror::Class*>(class_image_address));
        }
      }
    }
  }

  void CopyMethodArrays(ObjPtr<mirror::Class> cls,
                        uint32_t class_image_address,
                        bool is_class_initialized)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    size_t number_of_methods = cls->NumMethods();
    if (number_of_methods == 0) {
      return;
    }

    size_t size = LengthPrefixedArray<ArtMethod>::ComputeSize(number_of_methods);
    size_t offset = art_methods_.size();
    art_methods_.resize(offset + size);
    auto* dest_array =
        reinterpret_cast<LengthPrefixedArray<ArtMethod>*>(art_methods_.data() + offset);
    memcpy(dest_array, cls->GetMethodsPtr(), size);
    native_relocations_.Put(cls->GetMethodsPtr(),
                            std::make_pair(NativeRelocationKind::kArtMethodArray, offset));

    for (size_t i = 0; i != number_of_methods; ++i) {
      ArtMethod* method = &cls->GetMethodsPtr()->At(i);
      ArtMethod* copy = &dest_array->At(i);

      // Update the class pointer.
      ObjPtr<mirror::Class> declaring_class = method->GetDeclaringClass();
      if (declaring_class == cls) {
        copy->GetDeclaringClassAddressWithoutBarrier()->Assign(
            reinterpret_cast<mirror::Class*>(class_image_address));
      } else {
        DCHECK(method->IsCopied());
        if (!IsInBootImage(declaring_class.Ptr())) {
          DCHECK(classes_.find(declaring_class->GetClassDef()) != classes_.end());
          copy->GetDeclaringClassAddressWithoutBarrier()->Assign(
              reinterpret_cast<mirror::Class*>(
                  image_begin_ +
                  sizeof(ImageHeader) +
                  classes_.Get(declaring_class->GetClassDef())));
        }
      }

      // Record the native relocation of the method.
      uintptr_t copy_offset =
          reinterpret_cast<uintptr_t>(copy) - reinterpret_cast<uintptr_t>(art_methods_.data());
      native_relocations_.Put(method,
                              std::make_pair(NativeRelocationKind::kArtMethod, copy_offset));

      // Ignore the single-implementation info for abstract method.
      if (method->IsAbstract()) {
        copy->SetHasSingleImplementation(false);
        copy->SetSingleImplementation(nullptr, kRuntimePointerSize);
      }

      // Set the entrypoint and data pointer of the method.
      StubType stub;
      if (method->IsNative()) {
        stub = StubType::kQuickGenericJNITrampoline;
      } else if (!cls->IsVerified()) {
        stub = StubType::kQuickToInterpreterBridge;
      } else if (!is_class_initialized && method->NeedsClinitCheckBeforeCall()) {
        stub = StubType::kQuickResolutionTrampoline;
      } else if (interpreter::IsNterpSupported() && CanMethodUseNterp(method)) {
        stub = StubType::kNterpTrampoline;
      } else {
        stub = StubType::kQuickToInterpreterBridge;
      }
      const std::vector<gc::space::ImageSpace*>& image_spaces =
          Runtime::Current()->GetHeap()->GetBootImageSpaces();
      DCHECK(!image_spaces.empty());
      const OatFile* oat_file = image_spaces[0]->GetOatFile();
      DCHECK(oat_file != nullptr);
      const OatHeader& header = oat_file->GetOatHeader();
      copy->SetEntryPointFromQuickCompiledCode(header.GetOatAddress(stub));

      if (method->IsNative()) {
        StubType stub_type = method->IsCriticalNative()
            ? StubType::kJNIDlsymLookupCriticalTrampoline
            : StubType::kJNIDlsymLookupTrampoline;
        copy->SetEntryPointFromJni(header.GetOatAddress(stub_type));
      } else if (method->HasCodeItem()) {
        const uint8_t* code_item = reinterpret_cast<const uint8_t*>(method->GetCodeItem());
        DCHECK_GE(code_item, method->GetDexFile()->DataBegin());
        uint32_t code_item_offset = dchecked_integral_cast<uint32_t>(
            code_item - method->GetDexFile()->DataBegin());;
        copy->SetDataPtrSize(
            reinterpret_cast<const void*>(code_item_offset), kRuntimePointerSize);
      }
    }
  }

  void CopyImTable(ObjPtr<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
    ImTable* table = cls->GetImt(kRuntimePointerSize);

    // If the table is null or shared and/or already emitted, we can skip.
    if (table == nullptr || IsInBootImage(table) || HasNativeRelocation(table)) {
      return;
    }
    const size_t size = ImTable::SizeInBytes(kRuntimePointerSize);
    size_t offset = im_tables_.size();
    im_tables_.resize(offset + size);
    uint8_t* dest = im_tables_.data() + offset;
    memcpy(dest, table, size);
    native_relocations_.Put(table, std::make_pair(NativeRelocationKind::kImTable, offset));
  }

  bool HasNativeRelocation(void* ptr) const {
    return native_relocations_.find(ptr) != native_relocations_.end();
  }


  static void LoadClassesFromReferenceProfile(
      Thread* self,
      const dchecked_vector<Handle<mirror::DexCache>>& dex_caches)
          REQUIRES_SHARED(Locks::mutator_lock_) {
    AppInfo* app_info = Runtime::Current()->GetAppInfo();
    std::string profile_file = app_info->GetPrimaryApkReferenceProfile();

    if (profile_file.empty()) {
      return;
    }

    // Lock the file, it could be concurrently updated by the system. Don't block
    // as this is app startup sensitive.
    std::string error;
    ScopedFlock profile =
        LockedFile::Open(profile_file.c_str(), O_RDONLY, /*block=*/false, &error);

    if (profile == nullptr) {
      LOG(DEBUG) << "Couldn't lock the profile file " << profile_file << ": " << error;
      return;
    }

    ProfileCompilationInfo profile_info(/* for_boot_image= */ false);

    if (!profile_info.Load(profile->Fd())) {
      LOG(DEBUG) << "Could not load profile file";
      return;
    }

    StackHandleScope<1> hs(self);
    Handle<mirror::ClassLoader> class_loader =
        hs.NewHandle<mirror::ClassLoader>(dex_caches[0]->GetClassLoader());
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    ScopedTrace loading_classes("Loading classes from profile");
    for (auto dex_cache : dex_caches) {
      const DexFile* dex_file = dex_cache->GetDexFile();
      const ArenaSet<dex::TypeIndex>* class_types = profile_info.GetClasses(*dex_file);
      if (class_types == nullptr) {
        // This means the profile file did not reference the dex file, which is the case
        // if there's no classes and methods of that dex file in the profile.
        continue;
      }

      for (dex::TypeIndex idx : *class_types) {
        // The index is greater or equal to NumTypeIds if the type is an extra
        // descriptor, not referenced by the dex file.
        if (idx.index_ < dex_file->NumTypeIds()) {
          ObjPtr<mirror::Class> klass = class_linker->ResolveType(idx, dex_cache, class_loader);
          if (klass == nullptr) {
            self->ClearException();
            LOG(DEBUG) << "Failed to preload " << dex_file->PrettyType(idx);
            continue;
          }
        }
      }
    }
  }

  bool WriteObjects(std::string* error_msg) {
    ScopedTrace write_objects("Writing objects");
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    ScopedObjectAccess soa(Thread::Current());
    VariableSizedHandleScope handles(soa.Self());

    Handle<mirror::Class> object_array_class = handles.NewHandle(
        GetClassRoot<mirror::ObjectArray<mirror::Object>>(class_linker));

    Handle<mirror::ObjectArray<mirror::Object>> image_roots = handles.NewHandle(
        mirror::ObjectArray<mirror::Object>::Alloc(
            soa.Self(), object_array_class.Get(), ImageHeader::kImageRootsMax));

    if (image_roots == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    // Find the dex files that will be used for generating the app image.
    dchecked_vector<Handle<mirror::DexCache>> dex_caches;
    FindDexCaches(soa.Self(), dex_caches, handles);

    if (dex_caches.size() == 0) {
      *error_msg = "Did not find dex caches to generate an app image";
      return false;
    }
    const OatDexFile* oat_dex_file = dex_caches[0]->GetDexFile()->GetOatDexFile();
    VdexFile* vdex_file = oat_dex_file->GetOatFile()->GetVdexFile();
    // The first entry in `dex_caches` contains the location of the primary APK.
    dex_location_ = oat_dex_file->GetDexFileLocation();

    size_t number_of_dex_files = vdex_file->GetNumberOfDexFiles();
    if (number_of_dex_files != dex_caches.size()) {
      // This means some dex files haven't been executed. For simplicity, just
      // register them and recollect dex caches.
      Handle<mirror::ClassLoader> loader = handles.NewHandle(dex_caches[0]->GetClassLoader());
      VisitClassLoaderDexFiles(soa.Self(), loader, [&](const art::DexFile* dex_file)
          REQUIRES_SHARED(Locks::mutator_lock_) {
        class_linker->RegisterDexFile(*dex_file, dex_caches[0]->GetClassLoader());
        return true;  // Continue with other dex files.
      });
      dex_caches.clear();
      FindDexCaches(soa.Self(), dex_caches, handles);
      if (number_of_dex_files != dex_caches.size()) {
        *error_msg = "Number of dex caches does not match number of dex files in the primary APK";
        return false;
      }
    }

    // If classes referenced in the reference profile are not loaded, preload
    // them. This makes sure we generate a good runtime app image, even if this
    // current app run did not load all startup classes.
    LoadClassesFromReferenceProfile(soa.Self(), dex_caches);

    // We store the checksums of the dex files used at runtime. These can be
    // different compared to the vdex checksums due to compact dex.
    std::vector<uint32_t> checksums(number_of_dex_files);
    uint32_t checksum_index = 0;
    for (const OatDexFile* current_oat_dex_file : oat_dex_file->GetOatFile()->GetOatDexFiles()) {
      const DexFile::Header* header =
          reinterpret_cast<const DexFile::Header*>(current_oat_dex_file->GetDexFilePointer());
      checksums[checksum_index++] = header->checksum_;
    }
    DCHECK_EQ(checksum_index, number_of_dex_files);

    // Create the fake OatHeader to store the dependencies of the image.
    SafeMap<std::string, std::string> key_value_store;
    Runtime* runtime = Runtime::Current();
    key_value_store.Put(OatHeader::kApexVersionsKey, runtime->GetApexVersions());
    key_value_store.Put(OatHeader::kBootClassPathKey,
                        android::base::Join(runtime->GetBootClassPathLocations(), ':'));
    key_value_store.Put(OatHeader::kBootClassPathChecksumsKey,
                        runtime->GetBootClassPathChecksums());
    key_value_store.Put(OatHeader::kClassPathKey,
                        oat_dex_file->GetOatFile()->GetClassLoaderContext());
    key_value_store.Put(OatHeader::kConcurrentCopying,
                        gUseReadBarrier ? OatHeader::kTrueValue : OatHeader::kFalseValue);

    std::unique_ptr<const InstructionSetFeatures> isa_features =
        InstructionSetFeatures::FromCppDefines();
    std::unique_ptr<OatHeader> oat_header(
        OatHeader::Create(kRuntimeISA,
                          isa_features.get(),
                          number_of_dex_files,
                          &key_value_store));

    // Create the byte array containing the oat header and dex checksums.
    uint32_t checksums_size = checksums.size() * sizeof(uint32_t);
    Handle<mirror::ByteArray> header_data = handles.NewHandle(
        mirror::ByteArray::Alloc(soa.Self(), oat_header->GetHeaderSize() + checksums_size));

    if (header_data == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    memcpy(header_data->GetData(), oat_header.get(), oat_header->GetHeaderSize());
    memcpy(header_data->GetData() + oat_header->GetHeaderSize(), checksums.data(), checksums_size);

    // Create and populate the dex caches aray.
    Handle<mirror::ObjectArray<mirror::Object>> dex_cache_array = handles.NewHandle(
        mirror::ObjectArray<mirror::Object>::Alloc(
            soa.Self(), object_array_class.Get(), dex_caches.size()));

    if (dex_cache_array == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    for (uint32_t i = 0; i < dex_caches.size(); ++i) {
      dex_cache_array->Set(i, dex_caches[i].Get());
    }

    image_roots->Set(ImageHeader::kDexCaches, dex_cache_array.Get());
    image_roots->Set(ImageHeader::kClassRoots, class_linker->GetClassRoots());
    image_roots->Set(ImageHeader::kAppImageOatHeader, header_data.Get());

    {
      // Now that we have created all objects needed for the `image_roots`, copy
      // it into the buffer. Note that this will recursively copy all objects
      // contained in `image_roots`. That's acceptable as we don't have cycles,
      // nor a deep graph.
      ScopedAssertNoThreadSuspension sants("Writing runtime app image");
      CopyObject(image_roots.Get());
    }

    // Emit classes defined in the app class loader (which will also indirectly
    // emit dex caches and their arrays).
    EmitClasses(soa.Self(), dex_cache_array);

    return true;
  }

  class FixupVisitor {
   public:
    FixupVisitor(RuntimeImageHelper* image, size_t copy_offset)
        : image_(image), copy_offset_(copy_offset) {}

    // We do not visit native roots. These are handled with other logic.
    void VisitRootIfNonNull(
        [[maybe_unused]] mirror::CompressedReference<mirror::Object>* root) const {
      LOG(FATAL) << "UNREACHABLE";
    }
    void VisitRoot([[maybe_unused]] mirror::CompressedReference<mirror::Object>* root) const {
      LOG(FATAL) << "UNREACHABLE";
    }

    void operator()(ObjPtr<mirror::Object> obj,
                    MemberOffset offset,
                    bool is_static) const
        REQUIRES_SHARED(Locks::mutator_lock_) {
      // We don't copy static fields, they are being handled when we try to
      // initialize the class.
      ObjPtr<mirror::Object> ref =
          is_static ? nullptr : obj->GetFieldObject<mirror::Object>(offset);
      mirror::Object* address = image_->GetOrComputeImageAddress(ref);
      mirror::Object* copy =
          reinterpret_cast<mirror::Object*>(image_->objects_.data() + copy_offset_);
      copy->GetFieldObjectReferenceAddr<kVerifyNone>(offset)->Assign(address);
    }

    // java.lang.ref.Reference visitor.
    void operator()([[maybe_unused]] ObjPtr<mirror::Class> klass,
                    ObjPtr<mirror::Reference> ref) const REQUIRES_SHARED(Locks::mutator_lock_) {
      operator()(ref, mirror::Reference::ReferentOffset(), /* is_static */ false);
    }

   private:
    RuntimeImageHelper* image_;
    size_t copy_offset_;
  };

  template <typename T>
  void CopyNativeDexCacheArray(uint32_t num_entries,
                               uint32_t max_entries,
                               mirror::NativeArray<T>* array) {
    if (array == nullptr) {
      return;
    }

    bool only_startup = !mirror::DexCache::ShouldAllocateFullArray(num_entries, max_entries);
    ArenaVector<uint8_t>& data = only_startup ? metadata_ : dex_cache_arrays_;
    NativeRelocationKind relocation_kind = only_startup
        ? NativeRelocationKind::kStartupNativeDexCacheArray
        : NativeRelocationKind::kFullNativeDexCacheArray;

    size_t size = num_entries * sizeof(void*);
    // We need to reserve space to store `num_entries` because ImageSpace doesn't have
    // access to the dex files when relocating dex caches.
    size_t offset = RoundUp(data.size(), sizeof(void*)) + sizeof(uintptr_t);
    data.resize(RoundUp(data.size(), sizeof(void*)) + sizeof(uintptr_t) + size);
    reinterpret_cast<uintptr_t*>(data.data() + offset)[-1] = num_entries;

    // Copy each entry individually. We cannot use memcpy, as the entries may be
    // updated concurrently by other mutator threads.
    mirror::NativeArray<T>* copy = reinterpret_cast<mirror::NativeArray<T>*>(data.data() + offset);
    for (uint32_t i = 0; i < num_entries; ++i) {
      copy->Set(i, array->Get(i));
    }
    native_relocations_.Put(array, std::make_pair(relocation_kind, offset));
  }

  template <typename T>
  mirror::GcRootArray<T>* CreateGcRootDexCacheArray(uint32_t num_entries,
                                                    uint32_t max_entries,
                                                    mirror::GcRootArray<T>* array) {
    if (array == nullptr) {
      return nullptr;
    }
    bool only_startup = !mirror::DexCache::ShouldAllocateFullArray(num_entries, max_entries);
    ArenaVector<uint8_t>& data = only_startup ? metadata_ : dex_cache_arrays_;
    NativeRelocationKind relocation_kind = only_startup
        ? NativeRelocationKind::kStartupNativeDexCacheArray
        : NativeRelocationKind::kFullNativeDexCacheArray;
    size_t size = num_entries * sizeof(GcRoot<T>);
    // We need to reserve space to store `num_entries` because ImageSpace doesn't have
    // access to the dex files when relocating dex caches.
    static_assert(sizeof(GcRoot<T>) == sizeof(uint32_t));
    size_t offset = data.size() + sizeof(uint32_t);
    data.resize(data.size() + sizeof(uint32_t) + size);
    reinterpret_cast<uint32_t*>(data.data() + offset)[-1] = num_entries;
    native_relocations_.Put(array, std::make_pair(relocation_kind, offset));

    return reinterpret_cast<mirror::GcRootArray<T>*>(data.data() + offset);
  }
  static bool EmitDexCacheArrays() {
    // We need to treat dex cache arrays specially in an image for userfaultfd.
    // Disable for now. See b/270936884.
    return !gUseUserfaultfd;
  }

  uint32_t CopyDexCache(ObjPtr<mirror::DexCache> cache) REQUIRES_SHARED(Locks::mutator_lock_) {
    auto it = dex_caches_.find(cache->GetDexFile());
    if (it != dex_caches_.end()) {
      return it->second;
    }
    uint32_t offset = CopyObject(cache);
    dex_caches_.Put(cache->GetDexFile(), offset);
    // For dex caches, clear pointers to data that will be set at runtime.
    mirror::Object* copy = reinterpret_cast<mirror::Object*>(objects_.data() + offset);
    reinterpret_cast<mirror::DexCache*>(copy)->ResetNativeArrays();
    reinterpret_cast<mirror::DexCache*>(copy)->SetDexFile(nullptr);

    if (!EmitDexCacheArrays()) {
      return offset;
    }

    // Copy the ArtMethod array.
    mirror::NativeArray<ArtMethod>* resolved_methods = cache->GetResolvedMethodsArray();
    CopyNativeDexCacheArray(cache->GetDexFile()->NumMethodIds(),
                            mirror::DexCache::kDexCacheMethodCacheSize,
                            resolved_methods);
    // Store the array pointer in the dex cache, which will be relocated at the end.
    reinterpret_cast<mirror::DexCache*>(copy)->SetResolvedMethodsArray(resolved_methods);

    // Copy the ArtField array.
    mirror::NativeArray<ArtField>* resolved_fields = cache->GetResolvedFieldsArray();
    CopyNativeDexCacheArray(cache->GetDexFile()->NumFieldIds(),
                            mirror::DexCache::kDexCacheFieldCacheSize,
                            resolved_fields);
    // Store the array pointer in the dex cache, which will be relocated at the end.
    reinterpret_cast<mirror::DexCache*>(copy)->SetResolvedFieldsArray(resolved_fields);

    // Copy the type array.
    mirror::GcRootArray<mirror::Class>* resolved_types = cache->GetResolvedTypesArray();
    CreateGcRootDexCacheArray(cache->GetDexFile()->NumTypeIds(),
                              mirror::DexCache::kDexCacheTypeCacheSize,
                              resolved_types);
    // Store the array pointer in the dex cache, which will be relocated at the end.
    reinterpret_cast<mirror::DexCache*>(copy)->SetResolvedTypesArray(resolved_types);

    // Copy the string array.
    mirror::GcRootArray<mirror::String>* strings = cache->GetStringsArray();
    // Note: `new_strings` points to temporary data, and is only valid here.
    mirror::GcRootArray<mirror::String>* new_strings =
        CreateGcRootDexCacheArray(cache->GetDexFile()->NumStringIds(),
                                  mirror::DexCache::kDexCacheStringCacheSize,
                                  strings);
    // Store the array pointer in the dex cache, which will be relocated at the end.
    reinterpret_cast<mirror::DexCache*>(copy)->SetStringsArray(strings);

    // The code below copies new objects, so invalidate the address we have for
    // `copy`.
    copy = nullptr;
    if (strings != nullptr) {
      for (uint32_t i = 0; i < cache->GetDexFile()->NumStringIds(); ++i) {
        ObjPtr<mirror::String> str = strings->Get(i);
        if (str == nullptr || IsInBootImage(str.Ptr())) {
          new_strings->Set(i, str.Ptr());
        } else {
          uint32_t hash = static_cast<uint32_t>(str->GetStoredHashCode());
          DCHECK_EQ(hash, static_cast<uint32_t>(str->ComputeHashCode()))
              << "Dex cache strings should be interned";
          auto it2 = intern_table_.FindWithHash(str.Ptr(), hash);
          if (it2 == intern_table_.end()) {
            uint32_t string_offset = CopyObject(str);
            uint32_t address = image_begin_ + string_offset + sizeof(ImageHeader);
            intern_table_.InsertWithHash(address, hash);
            new_strings->Set(i, reinterpret_cast<mirror::String*>(address));
          } else {
            new_strings->Set(i, reinterpret_cast<mirror::String*>(*it2));
          }
          // To not confuse string references from the dex cache object and
          // string references from the array, we put an offset bigger than the
          // size of a DexCache object. ClassLinker::VisitInternedStringReferences
          // knows how to decode this offset.
          string_reference_offsets_.emplace_back(
              sizeof(ImageHeader) + offset, sizeof(mirror::DexCache) + i);
        }
      }
    }

    return offset;
  }

  bool IsInitialized(mirror::Class* cls) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (IsInBootImage(cls)) {
      const OatDexFile* oat_dex_file = cls->GetDexFile().GetOatDexFile();
      DCHECK(oat_dex_file != nullptr) << "We should always have an .oat file for a boot image";
      uint16_t class_def_index = cls->GetDexClassDefIndex();
      ClassStatus oat_file_class_status = oat_dex_file->GetOatClass(class_def_index).GetStatus();
      return oat_file_class_status == ClassStatus::kVisiblyInitialized;
    } else {
      return cls->IsVisiblyInitialized<kVerifyNone>();
    }
  }
  // Try to initialize `copy`. Note that `cls` may not be initialized.
  // This is called after the image generation logic has visited super classes
  // and super interfaces, so we can just check those directly.
  bool TryInitializeClass(mirror::Class* copy, ObjPtr<mirror::Class> cls, uint32_t class_offset)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!cls->IsVerified()) {
      return false;
    }
    if (cls->IsArrayClass()) {
      return true;
    }

    // Check if we have been able to initialize the super class.
    mirror::Class* super = GetClassContent(cls->GetSuperClass());
    DCHECK(super != nullptr)
        << "App image classes should always have a super class: " << cls->PrettyClass();
    if (!IsInitialized(super)) {
      return false;
    }

    // We won't initialize class with class initializers.
    if (cls->FindClassInitializer(kRuntimePointerSize) != nullptr) {
      return false;
    }

    // For non-interface classes, we require all implemented interfaces to be
    // initialized.
    if (!cls->IsInterface()) {
      for (size_t i = 0; i < cls->NumDirectInterfaces(); i++) {
        mirror::Class* itf = GetClassContent(cls->GetDirectInterface(i));
        if (!IsInitialized(itf)) {
          return false;
        }
      }
    }

    // Trivial case: no static fields.
    if (cls->NumStaticFields() == 0u) {
      return true;
    }

    // Go over all static fields and try to initialize them.
    EncodedStaticFieldValueIterator it(cls->GetDexFile(), *cls->GetClassDef());
    if (!it.HasNext()) {
      return true;
    }

    // Temporary string offsets in case we failed to initialize the class. We
    // will add the offsets at the end of this method if we are successful.
    ArenaVector<AppImageReferenceOffsetInfo> string_offsets(allocator_.Adapter());
    ClassLinker* linker = Runtime::Current()->GetClassLinker();
    ClassAccessor accessor(cls->GetDexFile(), *cls->GetClassDef());
    for (const ClassAccessor::Field& field : accessor.GetStaticFields()) {
      if (!it.HasNext()) {
        break;
      }
      ArtField* art_field = linker->LookupResolvedField(field.GetIndex(),
                                                        cls->GetDexCache(),
                                                        cls->GetClassLoader(),
                                                        /* is_static= */ true);
      DCHECK_NE(art_field, nullptr);
      MemberOffset offset(art_field->GetOffset());
      switch (it.GetValueType()) {
        case EncodedArrayValueIterator::ValueType::kBoolean:
          copy->SetFieldBoolean<false>(offset, it.GetJavaValue().z);
          break;
        case EncodedArrayValueIterator::ValueType::kByte:
          copy->SetFieldByte<false>(offset, it.GetJavaValue().b);
          break;
        case EncodedArrayValueIterator::ValueType::kShort:
          copy->SetFieldShort<false>(offset, it.GetJavaValue().s);
          break;
        case EncodedArrayValueIterator::ValueType::kChar:
          copy->SetFieldChar<false>(offset, it.GetJavaValue().c);
          break;
        case EncodedArrayValueIterator::ValueType::kInt:
          copy->SetField32<false>(offset, it.GetJavaValue().i);
          break;
        case EncodedArrayValueIterator::ValueType::kLong:
          copy->SetField64<false>(offset, it.GetJavaValue().j);
          break;
        case EncodedArrayValueIterator::ValueType::kFloat:
          copy->SetField32<false>(offset, it.GetJavaValue().i);
          break;
        case EncodedArrayValueIterator::ValueType::kDouble:
          copy->SetField64<false>(offset, it.GetJavaValue().j);
          break;
        case EncodedArrayValueIterator::ValueType::kNull:
          copy->SetFieldObject<false>(offset, nullptr);
          break;
        case EncodedArrayValueIterator::ValueType::kString: {
          ObjPtr<mirror::String> str =
              linker->LookupString(dex::StringIndex(it.GetJavaValue().i), cls->GetDexCache());
          mirror::String* str_copy = nullptr;
          if (str == nullptr) {
            // String wasn't created yet.
            return false;
          } else if (IsInBootImage(str.Ptr())) {
            str_copy = str.Ptr();
          } else {
            uint32_t hash = static_cast<uint32_t>(str->GetStoredHashCode());
            DCHECK_EQ(hash, static_cast<uint32_t>(str->ComputeHashCode()))
                << "Dex cache strings should be interned";
            auto string_it = intern_table_.FindWithHash(str.Ptr(), hash);
            if (string_it == intern_table_.end()) {
              // The string must be interned.
              uint32_t string_offset = CopyObject(str);
              // Reload the class copy after having copied the string.
              copy = reinterpret_cast<mirror::Class*>(objects_.data() + class_offset);
              uint32_t address = image_begin_ + string_offset + sizeof(ImageHeader);
              intern_table_.InsertWithHash(address, hash);
              str_copy = reinterpret_cast<mirror::String*>(address);
            } else {
              str_copy = reinterpret_cast<mirror::String*>(*string_it);
            }
            string_offsets.emplace_back(sizeof(ImageHeader) + class_offset, offset.Int32Value());
          }
          uint8_t* raw_addr = reinterpret_cast<uint8_t*>(copy) + offset.Int32Value();
          mirror::HeapReference<mirror::Object>* objref_addr =
              reinterpret_cast<mirror::HeapReference<mirror::Object>*>(raw_addr);
          objref_addr->Assign</* kIsVolatile= */ false>(str_copy);
          break;
        }
        case EncodedArrayValueIterator::ValueType::kType: {
          // Note that it may be that the referenced type hasn't been processed
          // yet by the image generation logic. In this case we bail out for
          // simplicity.
          ObjPtr<mirror::Class> type =
              linker->LookupResolvedType(dex::TypeIndex(it.GetJavaValue().i), cls);
          mirror::Class* type_copy = nullptr;
          if (type == nullptr) {
            // Class wasn't resolved yet.
            return false;
          } else if (IsInBootImage(type.Ptr())) {
            // Make sure the type is in our class table.
            uint32_t hash = type->DescriptorHash();
            class_table_.InsertWithHash(ClassTable::TableSlot(type.Ptr(), hash), hash);
            type_copy = type.Ptr();
          } else if (type->IsArrayClass()) {
            std::string class_name;
            type->GetDescriptor(&class_name);
            auto class_it = array_classes_.find(class_name);
            if (class_it == array_classes_.end()) {
              return false;
            }
            type_copy = reinterpret_cast<mirror::Class*>(
                image_begin_ + sizeof(ImageHeader) + class_it->second);
          } else {
            const dex::ClassDef* class_def = type->GetClassDef();
            DCHECK_NE(class_def, nullptr);
            auto class_it = classes_.find(class_def);
            if (class_it == classes_.end()) {
              return false;
            }
            type_copy = reinterpret_cast<mirror::Class*>(
                image_begin_ + sizeof(ImageHeader) + class_it->second);
          }
          uint8_t* raw_addr = reinterpret_cast<uint8_t*>(copy) + offset.Int32Value();
          mirror::HeapReference<mirror::Object>* objref_addr =
              reinterpret_cast<mirror::HeapReference<mirror::Object>*>(raw_addr);
          objref_addr->Assign</* kIsVolatile= */ false>(type_copy);
          break;
        }
        default:
          LOG(FATAL) << "Unreachable";
      }
      it.Next();
    }
    // We have successfully initialized the class, we can now record the string
    // offsets.
    string_reference_offsets_.insert(
        string_reference_offsets_.end(), string_offsets.begin(), string_offsets.end());
    return true;
  }

  uint32_t CopyClass(ObjPtr<mirror::Class> cls) REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(!cls->IsBootStrapClassLoaded());
    uint32_t offset = 0u;
    if (cls->IsArrayClass()) {
      std::string class_name;
      cls->GetDescriptor(&class_name);
      auto it = array_classes_.find(class_name);
      if (it != array_classes_.end()) {
        return it->second;
      }
      offset = CopyObject(cls);
      array_classes_.Put(class_name, offset);
    } else {
      const dex::ClassDef* class_def = cls->GetClassDef();
      auto it = classes_.find(class_def);
      if (it != classes_.end()) {
        return it->second;
      }
      offset = CopyObject(cls);
      classes_.Put(class_def, offset);
    }

    uint32_t hash = cls->DescriptorHash();
    // Save the hash, the `HashSet` implementation requires to find it.
    class_hashes_.Put(offset, hash);
    uint32_t class_image_address = image_begin_ + sizeof(ImageHeader) + offset;
    bool inserted =
        class_table_.InsertWithHash(ClassTable::TableSlot(class_image_address, hash), hash).second;
    DCHECK(inserted) << "Class " << cls->PrettyDescriptor()
                     << " (" << cls.Ptr() << ") already inserted";

    // Clear internal state.
    mirror::Class* copy = reinterpret_cast<mirror::Class*>(objects_.data() + offset);
    copy->SetClinitThreadId(static_cast<pid_t>(0u));
    if (cls->IsArrayClass()) {
      DCHECK(copy->IsVisiblyInitialized());
    } else {
      copy->SetStatusInternal(cls->IsVerified() ? ClassStatus::kVerified : ClassStatus::kResolved);
    }

    // Clear static field values.
    auto clear_class = [&] () REQUIRES_SHARED(Locks::mutator_lock_) {
      MemberOffset static_offset = cls->GetFirstReferenceStaticFieldOffset(kRuntimePointerSize);
      memset(objects_.data() + offset + static_offset.Uint32Value(),
             0,
             cls->GetClassSize() - static_offset.Uint32Value());
    };
    clear_class();

    bool is_class_initialized = TryInitializeClass(copy, cls, offset);
    // Reload the copy, it may have moved after `TryInitializeClass`.
    copy = reinterpret_cast<mirror::Class*>(objects_.data() + offset);
    if (is_class_initialized) {
      copy->SetStatusInternal(ClassStatus::kVisiblyInitialized);
      if (!cls->IsArrayClass() && !cls->IsFinalizable()) {
        copy->SetObjectSizeAllocFastPath(RoundUp(cls->GetObjectSize(), kObjectAlignment));
      }
      if (cls->IsInterface()) {
        copy->SetAccessFlags(copy->GetAccessFlags() | kAccRecursivelyInitialized);
      }
    } else {
      // If we fail to initialize, remove initialization related flags and
      // clear again.
      copy->SetObjectSizeAllocFastPath(std::numeric_limits<uint32_t>::max());
      copy->SetAccessFlags(copy->GetAccessFlags() & ~kAccRecursivelyInitialized);
      clear_class();
    }

    CopyFieldArrays(cls, class_image_address);
    CopyMethodArrays(cls, class_image_address, is_class_initialized);
    if (cls->ShouldHaveImt()) {
      CopyImTable(cls);
    }

    return offset;
  }

  // Copy `obj` in `objects_` and relocate references. Returns the offset
  // within our buffer.
  uint32_t CopyObject(ObjPtr<mirror::Object> obj) REQUIRES_SHARED(Locks::mutator_lock_) {
    // Copy the object in `objects_`.
    size_t object_size = obj->SizeOf();
    size_t offset = objects_.size();
    DCHECK(IsAligned<kObjectAlignment>(offset));
    object_offsets_.push_back(offset);
    objects_.resize(RoundUp(offset + object_size, kObjectAlignment));

    mirror::Object* copy = reinterpret_cast<mirror::Object*>(objects_.data() + offset);
    mirror::Object::CopyRawObjectData(
        reinterpret_cast<uint8_t*>(copy), obj, object_size - sizeof(mirror::Object));
    // Clear any lockword data.
    copy->SetLockWord(LockWord::Default(), /* as_volatile= */ false);
    copy->SetClass(obj->GetClass());

    // Fixup reference pointers.
    FixupVisitor visitor(this, offset);
    obj->VisitReferences</*kVisitNativeRoots=*/ false>(visitor, visitor);

    if (obj->IsString()) {
      // Ensure a string always has a hashcode stored. This is checked at
      // runtime because boot images don't want strings dirtied due to hashcode.
      reinterpret_cast<mirror::String*>(copy)->GetHashCode();
    }

    object_section_size_ += RoundUp(object_size, kObjectAlignment);
    return offset;
  }

  class CollectDexCacheVisitor : public DexCacheVisitor {
   public:
    explicit CollectDexCacheVisitor(VariableSizedHandleScope& handles) : handles_(handles) {}

    void Visit(ObjPtr<mirror::DexCache> dex_cache)
        REQUIRES_SHARED(Locks::dex_lock_, Locks::mutator_lock_) override {
      dex_caches_.push_back(handles_.NewHandle(dex_cache));
    }
    const std::vector<Handle<mirror::DexCache>>& GetDexCaches() const {
      return dex_caches_;
    }
   private:
    VariableSizedHandleScope& handles_;
    std::vector<Handle<mirror::DexCache>> dex_caches_;
  };

  // Find dex caches corresponding to the primary APK.
  void FindDexCaches(Thread* self,
                     dchecked_vector<Handle<mirror::DexCache>>& dex_caches,
                     VariableSizedHandleScope& handles)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    ScopedTrace trace("Find dex caches");
    DCHECK(dex_caches.empty());
    // Collect all dex caches.
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    CollectDexCacheVisitor visitor(handles);
    {
      ReaderMutexLock mu(self, *Locks::dex_lock_);
      class_linker->VisitDexCaches(&visitor);
    }

    // Find the primary APK.
    AppInfo* app_info = Runtime::Current()->GetAppInfo();
    for (Handle<mirror::DexCache> cache : visitor.GetDexCaches()) {
      if (app_info->GetRegisteredCodeType(cache->GetDexFile()->GetLocation()) ==
              AppInfo::CodeType::kPrimaryApk) {
        dex_caches.push_back(handles.NewHandle(cache.Get()));
        break;
      }
    }

    if (dex_caches.empty()) {
      return;
    }

    const OatDexFile* oat_dex_file = dex_caches[0]->GetDexFile()->GetOatDexFile();
    if (oat_dex_file == nullptr) {
      // We need a .oat file for loading an app image;
      dex_caches.clear();
      return;
    }

    // Store the dex caches in the order in which their corresponding dex files
    // are stored in the oat file. When we check for checksums at the point of
    // loading the image, we rely on this order.
    for (const OatDexFile* current : oat_dex_file->GetOatFile()->GetOatDexFiles()) {
      if (current != oat_dex_file) {
        for (Handle<mirror::DexCache> cache : visitor.GetDexCaches()) {
          if (cache->GetDexFile()->GetOatDexFile() == current) {
            dex_caches.push_back(handles.NewHandle(cache.Get()));
          }
        }
      }
    }
  }

  static uint64_t PointerToUint64(void* ptr) {
    return reinterpret_cast64<uint64_t>(ptr);
  }

  void WriteImageMethods() {
    ScopedObjectAccess soa(Thread::Current());
    // We can just use plain runtime pointers.
    Runtime* runtime = Runtime::Current();
    header_.image_methods_[ImageHeader::kResolutionMethod] =
        PointerToUint64(runtime->GetResolutionMethod());
    header_.image_methods_[ImageHeader::kImtConflictMethod] =
        PointerToUint64(runtime->GetImtConflictMethod());
    header_.image_methods_[ImageHeader::kImtUnimplementedMethod] =
        PointerToUint64(runtime->GetImtUnimplementedMethod());
    header_.image_methods_[ImageHeader::kSaveAllCalleeSavesMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveAllCalleeSaves));
    header_.image_methods_[ImageHeader::kSaveRefsOnlyMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveRefsOnly));
    header_.image_methods_[ImageHeader::kSaveRefsAndArgsMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveRefsAndArgs));
    header_.image_methods_[ImageHeader::kSaveEverythingMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverything));
    header_.image_methods_[ImageHeader::kSaveEverythingMethodForClinit] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverythingForClinit));
    header_.image_methods_[ImageHeader::kSaveEverythingMethodForSuspendCheck] =
        PointerToUint64(
            runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverythingForSuspendCheck));
  }

  // Header for the image, created at the end once we know the size of all
  // sections.
  ImageHeader header_;

  // Allocator for the various data structures to allocate while generating the
  // image.
  ArenaAllocator allocator_;

  // Contents of the various sections.
  ArenaVector<uint8_t> objects_;
  ArenaVector<uint8_t> art_fields_;
  ArenaVector<uint8_t> art_methods_;
  ArenaVector<uint8_t> im_tables_;
  ArenaVector<uint8_t> metadata_;
  ArenaVector<uint8_t> dex_cache_arrays_;

  ArenaVector<AppImageReferenceOffsetInfo> string_reference_offsets_;

  // Bitmap of live objects in `objects_`. Populated from `object_offsets_`
  // once we know `object_section_size`.
  gc::accounting::ContinuousSpaceBitmap image_bitmap_;

  // Sections stored in the header.
  ArenaVector<ImageSection> sections_;

  // A list of offsets in `objects_` where objects begin.
  ArenaVector<uint32_t> object_offsets_;

  ArenaSafeMap<const dex::ClassDef*, uint32_t> classes_;
  ArenaSafeMap<std::string, uint32_t> array_classes_;
  ArenaSafeMap<const DexFile*, uint32_t> dex_caches_;
  ArenaSafeMap<uint32_t, uint32_t> class_hashes_;

  ArenaSafeMap<void*, std::pair<NativeRelocationKind, uint32_t>> native_relocations_;

  // Cached values of boot image information.
  const uint32_t boot_image_begin_;
  const uint32_t boot_image_size_;

  // Where the image begins: just after the boot image.
  const uint32_t image_begin_;

  // Size of the `kSectionObjects` section.
  size_t object_section_size_;

  // The location of the primary APK / dex file.
  std::string dex_location_;

  // The intern table for strings that we will write to disk.
  InternTableSet intern_table_;

  // The class table holding classes that we will write to disk.
  ClassTableSet class_table_;

  friend class ClassDescriptorHash;
  friend class PruneVisitor;
  friend class NativePointerVisitor;
};

std::string RuntimeImage::GetRuntimeImageDir(const std::string& app_data_dir) {
  if (app_data_dir.empty()) {
    // The data directory is empty for tests.
    return "";
  }
  return app_data_dir + "/cache/oat_primary/";
}

// Note: this may return a relative path for tests.
std::string RuntimeImage::GetRuntimeImagePath(const std::string& app_data_dir,
                                              const std::string& dex_location,
                                              const std::string& isa) {
  std::string basename = android::base::Basename(dex_location);
  std::string filename = ReplaceFileExtension(basename, "art");

  return GetRuntimeImageDir(app_data_dir) + isa + "/" + filename;
}

std::string RuntimeImage::GetRuntimeImagePath(const std::string& dex_location) {
  return GetRuntimeImagePath(Runtime::Current()->GetProcessDataDirectory(),
                             dex_location,
                             GetInstructionSetString(kRuntimeISA));
}

static bool EnsureDirectoryExists(const std::string& directory, std::string* error_msg) {
  if (!OS::DirectoryExists(directory.c_str())) {
    static constexpr mode_t kDirectoryMode = S_IRWXU | S_IRGRP | S_IXGRP| S_IROTH | S_IXOTH;
    if (mkdir(directory.c_str(), kDirectoryMode) != 0) {
      *error_msg =
          StringPrintf("Could not create directory %s: %s", directory.c_str(), strerror(errno));
      return false;
    }
  }
  return true;
}

bool RuntimeImage::WriteImageToDisk(std::string* error_msg) {
  gc::Heap* heap = Runtime::Current()->GetHeap();
  if (!heap->HasBootImageSpace()) {
    *error_msg = "Cannot generate an app image without a boot image";
    return false;
  }
  std::string oat_path = GetRuntimeImageDir(Runtime::Current()->GetProcessDataDirectory());
  if (!oat_path.empty() && !EnsureDirectoryExists(oat_path, error_msg)) {
    return false;
  }

  ScopedTrace generate_image_trace("Generating runtime image");
  std::unique_ptr<RuntimeImageHelper> image(new RuntimeImageHelper(heap));
  if (!image->Generate(error_msg)) {
    return false;
  }

  ScopedTrace write_image_trace("Writing runtime image to disk");

  const std::string path = GetRuntimeImagePath(image->GetDexLocation());
  if (!EnsureDirectoryExists(android::base::Dirname(path), error_msg)) {
    return false;
  }

  // We first generate the app image in a temporary file, which we will then
  // move to `path`.
  const std::string temp_path = ReplaceFileExtension(path, std::to_string(getpid()) + ".tmp");
  ImageFileGuard image_file;
  image_file.reset(OS::CreateEmptyFileWriteOnly(temp_path.c_str()));

  if (image_file == nullptr) {
    *error_msg = "Could not open " + temp_path + " for writing";
    return false;
  }

  // Make the file read-only immediately after creation to prevent tampering
  // during the generation window. Writes to the open file descriptor will
  // still succeed.
  if (fchmod(image_file->Fd(), S_IRUSR | S_IRGRP | S_IROTH) != 0) {
    *error_msg = "Failed to make runtime image read-only: " + std::string(strerror(errno));
    return false;
  }

  std::vector<uint8_t> full_data(image->GetHeader()->GetImageSize());
  image->FillData(full_data);

  // Specify default block size of 512K to enable parallel image decompression.
  static constexpr size_t kMaxImageBlockSize = 524288;
  // Use LZ4 as good compromise between CPU time and compression. LZ4HC
  // empirically takes 10x more time compressing.
  static constexpr ImageHeader::StorageMode kImageStorageMode = ImageHeader::kStorageModeLZ4;
  // Note: no need to update the checksum of the runtime app image: we have no
  // use for it, and computing it takes CPU time.
  if (!image->GetHeader()->WriteData(
          image_file,
          full_data.data(),
          reinterpret_cast<const uint8_t*>(image->GetImageBitmap().Begin()),
          kImageStorageMode,
          kMaxImageBlockSize,
          /* update_checksum= */ false,
          error_msg)) {
    return false;
  }

  if (!image_file.WriteHeaderAndClose(temp_path, image->GetHeader(), error_msg)) {
    return false;
  }

  if (rename(temp_path.c_str(), path.c_str()) != 0) {
    *error_msg =
        "Failed to move runtime app image to " + path + ": " + std::string(strerror(errno));
    // Unlink directly: we cannot use `out` as we have closed it.
    unlink(temp_path.c_str());
    return false;
  }

  return true;
}

}  // namespace art
