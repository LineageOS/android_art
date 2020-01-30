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

#include "debugger.h"

#include <sys/uio.h>

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "android-base/macros.h"
#include "android-base/stringprintf.h"

#include "arch/context.h"
#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/endian_utils.h"
#include "base/enums.h"
#include "base/logging.h"
#include "base/memory_tool.h"
#include "base/safe_map.h"
#include "base/strlcpy.h"
#include "base/time_utils.h"
#include "class_linker-inl.h"
#include "class_linker.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_annotations.h"
#include "dex/dex_file_types.h"
#include "dex/dex_instruction.h"
#include "dex/utf.h"
#include "entrypoints/runtime_asm_entrypoints.h"
#include "gc/accounting/card_table-inl.h"
#include "gc/allocation_record.h"
#include "gc/gc_cause.h"
#include "gc/scoped_gc_critical_section.h"
#include "gc/space/bump_pointer_space-walk-inl.h"
#include "gc/space/large_object_space.h"
#include "gc/space/space-inl.h"
#include "handle_scope-inl.h"
#include "instrumentation.h"
#include "jni/jni_internal.h"
#include "jvalue-inl.h"
#include "mirror/array-alloc-inl.h"
#include "mirror/class-alloc-inl.h"
#include "mirror/class-inl.h"
#include "mirror/class.h"
#include "mirror/class_loader.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/string-alloc-inl.h"
#include "mirror/string-inl.h"
#include "mirror/throwable.h"
#include "nativehelper/scoped_local_ref.h"
#include "nativehelper/scoped_primitive_array.h"
#include "oat_file.h"
#include "obj_ptr-inl.h"
#include "reflection.h"
#include "reflective_handle.h"
#include "reflective_handle_scope-inl.h"
#include "runtime-inl.h"
#include "runtime_callbacks.h"
#include "scoped_thread_state_change-inl.h"
#include "scoped_thread_state_change.h"
#include "stack.h"
#include "thread.h"
#include "thread_list.h"
#include "thread_pool.h"
#include "well_known_classes.h"

namespace art {

using android::base::StringPrintf;

// Limit alloc_record_count to the 2BE value (64k-1) that is the limit of the current protocol.
static uint16_t CappedAllocRecordCount(size_t alloc_record_count) {
  const size_t cap = 0xffff;
  if (alloc_record_count > cap) {
    return cap;
  }
  return alloc_record_count;
}

// JDWP is allowed unless the Zygote forbids it.
static bool gJdwpAllowed = true;

static bool gDdmThreadNotification = false;

// DDMS GC-related settings.
static Dbg::HpifWhen gDdmHpifWhen = Dbg::HPIF_WHEN_NEVER;
static Dbg::HpsgWhen gDdmHpsgWhen = Dbg::HPSG_WHEN_NEVER;
static Dbg::HpsgWhat gDdmHpsgWhat;
static Dbg::HpsgWhen gDdmNhsgWhen = Dbg::HPSG_WHEN_NEVER;
static Dbg::HpsgWhat gDdmNhsgWhat;

Dbg::DbgThreadLifecycleCallback Dbg::thread_lifecycle_callback_;

void Dbg::GcDidFinish() {
  if (gDdmHpifWhen != HPIF_WHEN_NEVER) {
    ScopedObjectAccess soa(Thread::Current());
    VLOG(jdwp) << "Sending heap info to DDM";
    DdmSendHeapInfo(gDdmHpifWhen);
  }
  if (gDdmHpsgWhen != HPSG_WHEN_NEVER) {
    ScopedObjectAccess soa(Thread::Current());
    VLOG(jdwp) << "Dumping heap to DDM";
    DdmSendHeapSegments(false);
  }
  if (gDdmNhsgWhen != HPSG_WHEN_NEVER) {
    ScopedObjectAccess soa(Thread::Current());
    VLOG(jdwp) << "Dumping native heap to DDM";
    DdmSendHeapSegments(true);
  }
}

void Dbg::SetJdwpAllowed(bool allowed) {
  gJdwpAllowed = allowed;
}

bool Dbg::IsJdwpAllowed() {
  return gJdwpAllowed;
}

// Do we need to deoptimize the stack to handle an exception?
bool Dbg::IsForcedInterpreterNeededForExceptionImpl(Thread* thread) {
  // Deoptimization is required if at least one method in the stack needs it. However we
  // skip frames that will be unwound (thus not executed).
  bool needs_deoptimization = false;
  StackVisitor::WalkStack(
      [&](art::StackVisitor* visitor) REQUIRES_SHARED(Locks::mutator_lock_) {
        // The visitor is meant to be used when handling exception from compiled code only.
        CHECK(!visitor->IsShadowFrame()) << "We only expect to visit compiled frame: "
                                         << ArtMethod::PrettyMethod(visitor->GetMethod());
        ArtMethod* method = visitor->GetMethod();
        if (method == nullptr) {
          // We reach an upcall and don't need to deoptimize this part of the stack (ManagedFragment)
          // so we can stop the visit.
          DCHECK(!needs_deoptimization);
          return false;
        }
        if (Runtime::Current()->GetInstrumentation()->InterpretOnly()) {
          // We found a compiled frame in the stack but instrumentation is set to interpret
          // everything: we need to deoptimize.
          needs_deoptimization = true;
          return false;
        }
        if (Runtime::Current()->GetInstrumentation()->IsDeoptimized(method)) {
          // We found a deoptimized method in the stack.
          needs_deoptimization = true;
          return false;
        }
        ShadowFrame* frame = visitor->GetThread()->FindDebuggerShadowFrame(visitor->GetFrameId());
        if (frame != nullptr) {
          // The debugger allocated a ShadowFrame to update a variable in the stack: we need to
          // deoptimize the stack to execute (and deallocate) this frame.
          needs_deoptimization = true;
          return false;
        }
        return true;
      },
      thread,
      /* context= */ nullptr,
      art::StackVisitor::StackWalkKind::kIncludeInlinedFrames,
      /* check_suspended */ true,
      /* include_transitions */ true);
  return needs_deoptimization;
}


bool Dbg::DdmHandleChunk(JNIEnv* env,
                         uint32_t type,
                         const ArrayRef<const jbyte>& data,
                         /*out*/uint32_t* out_type,
                         /*out*/std::vector<uint8_t>* out_data) {
  ScopedLocalRef<jbyteArray> dataArray(env, env->NewByteArray(data.size()));
  if (dataArray.get() == nullptr) {
    LOG(WARNING) << "byte[] allocation failed: " << data.size();
    env->ExceptionClear();
    return false;
  }
  env->SetByteArrayRegion(dataArray.get(),
                          0,
                          data.size(),
                          reinterpret_cast<const jbyte*>(data.data()));
  // Call "private static Chunk dispatch(int type, byte[] data, int offset, int length)".
  ScopedLocalRef<jobject> chunk(
      env,
      env->CallStaticObjectMethod(
          WellKnownClasses::org_apache_harmony_dalvik_ddmc_DdmServer,
          WellKnownClasses::org_apache_harmony_dalvik_ddmc_DdmServer_dispatch,
          type, dataArray.get(), 0, data.size()));
  if (env->ExceptionCheck()) {
    Thread* self = Thread::Current();
    ScopedObjectAccess soa(self);
    LOG(INFO) << StringPrintf("Exception thrown by dispatcher for 0x%08x", type) << std::endl
              << self->GetException()->Dump();
    self->ClearException();
    return false;
  }

  if (chunk.get() == nullptr) {
    return false;
  }

  /*
   * Pull the pieces out of the chunk.  We copy the results into a
   * newly-allocated buffer that the caller can free.  We don't want to
   * continue using the Chunk object because nothing has a reference to it.
   *
   * We could avoid this by returning type/data/offset/length and having
   * the caller be aware of the object lifetime issues, but that
   * integrates the JDWP code more tightly into the rest of the runtime, and doesn't work
   * if we have responses for multiple chunks.
   *
   * So we're pretty much stuck with copying data around multiple times.
   */
  ScopedLocalRef<jbyteArray> replyData(
      env,
      reinterpret_cast<jbyteArray>(
          env->GetObjectField(
              chunk.get(), WellKnownClasses::org_apache_harmony_dalvik_ddmc_Chunk_data)));
  jint offset = env->GetIntField(chunk.get(),
                                 WellKnownClasses::org_apache_harmony_dalvik_ddmc_Chunk_offset);
  jint length = env->GetIntField(chunk.get(),
                                 WellKnownClasses::org_apache_harmony_dalvik_ddmc_Chunk_length);
  *out_type = env->GetIntField(chunk.get(),
                               WellKnownClasses::org_apache_harmony_dalvik_ddmc_Chunk_type);

  VLOG(jdwp) << StringPrintf("DDM reply: type=0x%08x data=%p offset=%d length=%d",
                             type,
                             replyData.get(),
                             offset,
                             length);
  out_data->resize(length);
  env->GetByteArrayRegion(replyData.get(),
                          offset,
                          length,
                          reinterpret_cast<jbyte*>(out_data->data()));

  if (env->ExceptionCheck()) {
    Thread* self = Thread::Current();
    ScopedObjectAccess soa(self);
    LOG(INFO) << StringPrintf("Exception thrown when reading response data from dispatcher 0x%08x",
                              type) << std::endl << self->GetException()->Dump();
    self->ClearException();
    return false;
  }

  return true;
}

void Dbg::DdmBroadcast(bool connect) {
  VLOG(jdwp) << "Broadcasting DDM " << (connect ? "connect" : "disconnect") << "...";

  Thread* self = Thread::Current();
  if (self->GetState() != kRunnable) {
    LOG(ERROR) << "DDM broadcast in thread state " << self->GetState();
    /* try anyway? */
  }

  JNIEnv* env = self->GetJniEnv();
  jint event = connect ? 1 /*DdmServer.CONNECTED*/ : 2 /*DdmServer.DISCONNECTED*/;
  env->CallStaticVoidMethod(WellKnownClasses::org_apache_harmony_dalvik_ddmc_DdmServer,
                            WellKnownClasses::org_apache_harmony_dalvik_ddmc_DdmServer_broadcast,
                            event);
  if (env->ExceptionCheck()) {
    LOG(ERROR) << "DdmServer.broadcast " << event << " failed";
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
}

void Dbg::DdmConnected() {
  Dbg::DdmBroadcast(true);
}

void Dbg::DdmDisconnected() {
  Dbg::DdmBroadcast(false);
  gDdmThreadNotification = false;
}


/*
 * Send a notification when a thread starts, stops, or changes its name.
 *
 * Because we broadcast the full set of threads when the notifications are
 * first enabled, it's possible for "thread" to be actively executing.
 */
void Dbg::DdmSendThreadNotification(Thread* t, uint32_t type) {
  Locks::mutator_lock_->AssertNotExclusiveHeld(Thread::Current());
  if (!gDdmThreadNotification) {
    return;
  }

  RuntimeCallbacks* cb = Runtime::Current()->GetRuntimeCallbacks();
  if (type == CHUNK_TYPE("THDE")) {
    uint8_t buf[4];
    Set4BE(&buf[0], t->GetThreadId());
    cb->DdmPublishChunk(CHUNK_TYPE("THDE"), ArrayRef<const uint8_t>(buf));
  } else {
    CHECK(type == CHUNK_TYPE("THCR") || type == CHUNK_TYPE("THNM")) << type;
    StackHandleScope<1> hs(Thread::Current());
    Handle<mirror::String> name(hs.NewHandle(t->GetThreadName()));
    size_t char_count = (name != nullptr) ? name->GetLength() : 0;
    const jchar* chars = (name != nullptr) ? name->GetValue() : nullptr;
    bool is_compressed = (name != nullptr) ? name->IsCompressed() : false;

    std::vector<uint8_t> bytes;
    Append4BE(bytes, t->GetThreadId());
    if (is_compressed) {
      const uint8_t* chars_compressed = name->GetValueCompressed();
      AppendUtf16CompressedBE(bytes, chars_compressed, char_count);
    } else {
      AppendUtf16BE(bytes, chars, char_count);
    }
    CHECK_EQ(bytes.size(), char_count*2 + sizeof(uint32_t)*2);
    cb->DdmPublishChunk(type, ArrayRef<const uint8_t>(bytes));
  }
}

void Dbg::DdmSetThreadNotification(bool enable) {
  // Enable/disable thread notifications.
  gDdmThreadNotification = enable;
  if (enable) {
    // Use a Checkpoint to cause every currently running thread to send their own notification when
    // able. We then wait for every thread thread active at the time to post the creation
    // notification. Threads created later will send this themselves.
    Thread* self = Thread::Current();
    ScopedObjectAccess soa(self);
    Barrier finish_barrier(0);
    FunctionClosure fc([&](Thread* thread) REQUIRES_SHARED(Locks::mutator_lock_) {
      Thread* cls_self = Thread::Current();
      Locks::mutator_lock_->AssertSharedHeld(cls_self);
      Dbg::DdmSendThreadNotification(thread, CHUNK_TYPE("THCR"));
      finish_barrier.Pass(cls_self);
    });
    size_t checkpoints = Runtime::Current()->GetThreadList()->RunCheckpoint(&fc);
    ScopedThreadSuspension sts(self, ThreadState::kWaitingForCheckPointsToRun);
    finish_barrier.Increment(self, checkpoints);
  }
}

void Dbg::PostThreadStartOrStop(Thread* t, uint32_t type) {
  Dbg::DdmSendThreadNotification(t, type);
}

void Dbg::PostThreadStart(Thread* t) {
  Dbg::PostThreadStartOrStop(t, CHUNK_TYPE("THCR"));
}

void Dbg::PostThreadDeath(Thread* t) {
  Dbg::PostThreadStartOrStop(t, CHUNK_TYPE("THDE"));
}

int Dbg::DdmHandleHpifChunk(HpifWhen when) {
  if (when == HPIF_WHEN_NOW) {
    DdmSendHeapInfo(when);
    return 1;
  }

  if (when != HPIF_WHEN_NEVER && when != HPIF_WHEN_NEXT_GC && when != HPIF_WHEN_EVERY_GC) {
    LOG(ERROR) << "invalid HpifWhen value: " << static_cast<int>(when);
    return 0;
  }

  gDdmHpifWhen = when;
  return 1;
}

bool Dbg::DdmHandleHpsgNhsgChunk(Dbg::HpsgWhen when, Dbg::HpsgWhat what, bool native) {
  if (when != HPSG_WHEN_NEVER && when != HPSG_WHEN_EVERY_GC) {
    LOG(ERROR) << "invalid HpsgWhen value: " << static_cast<int>(when);
    return false;
  }

  if (what != HPSG_WHAT_MERGED_OBJECTS && what != HPSG_WHAT_DISTINCT_OBJECTS) {
    LOG(ERROR) << "invalid HpsgWhat value: " << static_cast<int>(what);
    return false;
  }

  if (native) {
    gDdmNhsgWhen = when;
    gDdmNhsgWhat = what;
  } else {
    gDdmHpsgWhen = when;
    gDdmHpsgWhat = what;
  }
  return true;
}

void Dbg::DdmSendHeapInfo(HpifWhen reason) {
  // If there's a one-shot 'when', reset it.
  if (reason == gDdmHpifWhen) {
    if (gDdmHpifWhen == HPIF_WHEN_NEXT_GC) {
      gDdmHpifWhen = HPIF_WHEN_NEVER;
    }
  }

  /*
   * Chunk HPIF (client --> server)
   *
   * Heap Info. General information about the heap,
   * suitable for a summary display.
   *
   *   [u4]: number of heaps
   *
   *   For each heap:
   *     [u4]: heap ID
   *     [u8]: timestamp in ms since Unix epoch
   *     [u1]: capture reason (same as 'when' value from server)
   *     [u4]: max heap size in bytes (-Xmx)
   *     [u4]: current heap size in bytes
   *     [u4]: current number of bytes allocated
   *     [u4]: current number of objects allocated
   */
  uint8_t heap_count = 1;
  gc::Heap* heap = Runtime::Current()->GetHeap();
  std::vector<uint8_t> bytes;
  Append4BE(bytes, heap_count);
  Append4BE(bytes, 1);  // Heap id (bogus; we only have one heap).
  Append8BE(bytes, MilliTime());
  Append1BE(bytes, reason);
  Append4BE(bytes, heap->GetMaxMemory());  // Max allowed heap size in bytes.
  Append4BE(bytes, heap->GetTotalMemory());  // Current heap size in bytes.
  Append4BE(bytes, heap->GetBytesAllocated());
  Append4BE(bytes, heap->GetObjectsAllocated());
  CHECK_EQ(bytes.size(), 4U + (heap_count * (4 + 8 + 1 + 4 + 4 + 4 + 4)));
  Runtime::Current()->GetRuntimeCallbacks()->DdmPublishChunk(CHUNK_TYPE("HPIF"),
                                                             ArrayRef<const uint8_t>(bytes));
}

enum HpsgSolidity {
  SOLIDITY_FREE = 0,
  SOLIDITY_HARD = 1,
  SOLIDITY_SOFT = 2,
  SOLIDITY_WEAK = 3,
  SOLIDITY_PHANTOM = 4,
  SOLIDITY_FINALIZABLE = 5,
  SOLIDITY_SWEEP = 6,
};

enum HpsgKind {
  KIND_OBJECT = 0,
  KIND_CLASS_OBJECT = 1,
  KIND_ARRAY_1 = 2,
  KIND_ARRAY_2 = 3,
  KIND_ARRAY_4 = 4,
  KIND_ARRAY_8 = 5,
  KIND_UNKNOWN = 6,
  KIND_NATIVE = 7,
};

#define HPSG_PARTIAL (1<<7)
#define HPSG_STATE(solidity, kind) ((uint8_t)((((kind) & 0x7) << 3) | ((solidity) & 0x7)))

class HeapChunkContext {
 public:
  // Maximum chunk size.  Obtain this from the formula:
  // (((maximum_heap_size / ALLOCATION_UNIT_SIZE) + 255) / 256) * 2
  HeapChunkContext(bool merge, bool native)
      : buf_(16384 - 16),
        type_(0),
        chunk_overhead_(0) {
    Reset();
    if (native) {
      type_ = CHUNK_TYPE("NHSG");
    } else {
      type_ = merge ? CHUNK_TYPE("HPSG") : CHUNK_TYPE("HPSO");
    }
  }

  ~HeapChunkContext() {
    if (p_ > &buf_[0]) {
      Flush();
    }
  }

  void SetChunkOverhead(size_t chunk_overhead) {
    chunk_overhead_ = chunk_overhead;
  }

  void ResetStartOfNextChunk() {
    startOfNextMemoryChunk_ = nullptr;
  }

  void EnsureHeader(const void* chunk_ptr) {
    if (!needHeader_) {
      return;
    }

    // Start a new HPSx chunk.
    Write4BE(&p_, 1);  // Heap id (bogus; we only have one heap).
    Write1BE(&p_, 8);  // Size of allocation unit, in bytes.

    Write4BE(&p_, reinterpret_cast<uintptr_t>(chunk_ptr));  // virtual address of segment start.
    Write4BE(&p_, 0);  // offset of this piece (relative to the virtual address).
    // [u4]: length of piece, in allocation units
    // We won't know this until we're done, so save the offset and stuff in a dummy value.
    pieceLenField_ = p_;
    Write4BE(&p_, 0x55555555);
    needHeader_ = false;
  }

  void Flush() REQUIRES_SHARED(Locks::mutator_lock_) {
    if (pieceLenField_ == nullptr) {
      // Flush immediately post Reset (maybe back-to-back Flush). Ignore.
      CHECK(needHeader_);
      return;
    }
    // Patch the "length of piece" field.
    CHECK_LE(&buf_[0], pieceLenField_);
    CHECK_LE(pieceLenField_, p_);
    Set4BE(pieceLenField_, totalAllocationUnits_);

    ArrayRef<const uint8_t> out(&buf_[0], p_ - &buf_[0]);
    Runtime::Current()->GetRuntimeCallbacks()->DdmPublishChunk(type_, out);
    Reset();
  }

  static void HeapChunkJavaCallback(void* start, void* end, size_t used_bytes, void* arg)
      REQUIRES_SHARED(Locks::heap_bitmap_lock_,
                            Locks::mutator_lock_) {
    reinterpret_cast<HeapChunkContext*>(arg)->HeapChunkJavaCallback(start, end, used_bytes);
  }

  static void HeapChunkNativeCallback(void* start, void* end, size_t used_bytes, void* arg)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    reinterpret_cast<HeapChunkContext*>(arg)->HeapChunkNativeCallback(start, end, used_bytes);
  }

 private:
  enum { ALLOCATION_UNIT_SIZE = 8 };

  void Reset() {
    p_ = &buf_[0];
    ResetStartOfNextChunk();
    totalAllocationUnits_ = 0;
    needHeader_ = true;
    pieceLenField_ = nullptr;
  }

  bool IsNative() const {
    return type_ == CHUNK_TYPE("NHSG");
  }

  // Returns true if the object is not an empty chunk.
  bool ProcessRecord(void* start, size_t used_bytes) REQUIRES_SHARED(Locks::mutator_lock_) {
    // Note: heap call backs cannot manipulate the heap upon which they are crawling, care is taken
    // in the following code not to allocate memory, by ensuring buf_ is of the correct size
    if (used_bytes == 0) {
      if (start == nullptr) {
        // Reset for start of new heap.
        startOfNextMemoryChunk_ = nullptr;
        Flush();
      }
      // Only process in use memory so that free region information
      // also includes dlmalloc book keeping.
      return false;
    }
    if (startOfNextMemoryChunk_ != nullptr) {
      // Transmit any pending free memory. Native free memory of over kMaxFreeLen could be because
      // of the use of mmaps, so don't report. If not free memory then start a new segment.
      bool flush = true;
      if (start > startOfNextMemoryChunk_) {
        const size_t kMaxFreeLen = 2 * kPageSize;
        void* free_start = startOfNextMemoryChunk_;
        void* free_end = start;
        const size_t free_len =
            reinterpret_cast<uintptr_t>(free_end) - reinterpret_cast<uintptr_t>(free_start);
        if (!IsNative() || free_len < kMaxFreeLen) {
          AppendChunk(HPSG_STATE(SOLIDITY_FREE, 0), free_start, free_len, IsNative());
          flush = false;
        }
      }
      if (flush) {
        startOfNextMemoryChunk_ = nullptr;
        Flush();
      }
    }
    return true;
  }

  void HeapChunkNativeCallback(void* start, void* /*end*/, size_t used_bytes)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (ProcessRecord(start, used_bytes)) {
      uint8_t state = ExamineNativeObject(start);
      AppendChunk(state, start, used_bytes + chunk_overhead_, /*is_native=*/ true);
      startOfNextMemoryChunk_ = reinterpret_cast<char*>(start) + used_bytes + chunk_overhead_;
    }
  }

  void HeapChunkJavaCallback(void* start, void* /*end*/, size_t used_bytes)
      REQUIRES_SHARED(Locks::heap_bitmap_lock_, Locks::mutator_lock_) {
    if (ProcessRecord(start, used_bytes)) {
      // Determine the type of this chunk.
      // OLD-TODO: if context.merge, see if this chunk is different from the last chunk.
      // If it's the same, we should combine them.
      uint8_t state = ExamineJavaObject(reinterpret_cast<mirror::Object*>(start));
      AppendChunk(state, start, used_bytes + chunk_overhead_, /*is_native=*/ false);
      startOfNextMemoryChunk_ = reinterpret_cast<char*>(start) + used_bytes + chunk_overhead_;
    }
  }

  void AppendChunk(uint8_t state, void* ptr, size_t length, bool is_native)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    // Make sure there's enough room left in the buffer.
    // We need to use two bytes for every fractional 256 allocation units used by the chunk plus
    // 17 bytes for any header.
    const size_t needed = ((RoundUp(length / ALLOCATION_UNIT_SIZE, 256) / 256) * 2) + 17;
    size_t byte_left = &buf_.back() - p_;
    if (byte_left < needed) {
      if (is_native) {
      // Cannot trigger memory allocation while walking native heap.
        return;
      }
      Flush();
    }

    byte_left = &buf_.back() - p_;
    if (byte_left < needed) {
      LOG(WARNING) << "Chunk is too big to transmit (chunk_len=" << length << ", "
          << needed << " bytes)";
      return;
    }
    EnsureHeader(ptr);
    // Write out the chunk description.
    length /= ALLOCATION_UNIT_SIZE;   // Convert to allocation units.
    totalAllocationUnits_ += length;
    while (length > 256) {
      *p_++ = state | HPSG_PARTIAL;
      *p_++ = 255;     // length - 1
      length -= 256;
    }
    *p_++ = state;
    *p_++ = length - 1;
  }

  uint8_t ExamineNativeObject(const void* p) REQUIRES_SHARED(Locks::mutator_lock_) {
    return p == nullptr ? HPSG_STATE(SOLIDITY_FREE, 0) : HPSG_STATE(SOLIDITY_HARD, KIND_NATIVE);
  }

  uint8_t ExamineJavaObject(ObjPtr<mirror::Object> o)
      REQUIRES_SHARED(Locks::mutator_lock_, Locks::heap_bitmap_lock_) {
    if (o == nullptr) {
      return HPSG_STATE(SOLIDITY_FREE, 0);
    }
    // It's an allocated chunk. Figure out what it is.
    gc::Heap* heap = Runtime::Current()->GetHeap();
    if (!heap->IsLiveObjectLocked(o)) {
      LOG(ERROR) << "Invalid object in managed heap: " << o;
      return HPSG_STATE(SOLIDITY_HARD, KIND_NATIVE);
    }
    ObjPtr<mirror::Class> c = o->GetClass();
    if (c == nullptr) {
      // The object was probably just created but hasn't been initialized yet.
      return HPSG_STATE(SOLIDITY_HARD, KIND_OBJECT);
    }
    if (!heap->IsValidObjectAddress(c.Ptr())) {
      LOG(ERROR) << "Invalid class for managed heap object: " << o << " " << c;
      return HPSG_STATE(SOLIDITY_HARD, KIND_UNKNOWN);
    }
    if (c->GetClass() == nullptr) {
      LOG(ERROR) << "Null class of class " << c << " for object " << o;
      return HPSG_STATE(SOLIDITY_HARD, KIND_UNKNOWN);
    }
    if (c->IsClassClass()) {
      return HPSG_STATE(SOLIDITY_HARD, KIND_CLASS_OBJECT);
    }
    if (c->IsArrayClass()) {
      switch (c->GetComponentSize()) {
      case 1: return HPSG_STATE(SOLIDITY_HARD, KIND_ARRAY_1);
      case 2: return HPSG_STATE(SOLIDITY_HARD, KIND_ARRAY_2);
      case 4: return HPSG_STATE(SOLIDITY_HARD, KIND_ARRAY_4);
      case 8: return HPSG_STATE(SOLIDITY_HARD, KIND_ARRAY_8);
      }
    }
    return HPSG_STATE(SOLIDITY_HARD, KIND_OBJECT);
  }

  std::vector<uint8_t> buf_;
  uint8_t* p_;
  uint8_t* pieceLenField_;
  void* startOfNextMemoryChunk_;
  size_t totalAllocationUnits_;
  uint32_t type_;
  bool needHeader_;
  size_t chunk_overhead_;

  DISALLOW_COPY_AND_ASSIGN(HeapChunkContext);
};


void Dbg::DdmSendHeapSegments(bool native) {
  Dbg::HpsgWhen when = native ? gDdmNhsgWhen : gDdmHpsgWhen;
  Dbg::HpsgWhat what = native ? gDdmNhsgWhat : gDdmHpsgWhat;
  if (when == HPSG_WHEN_NEVER) {
    return;
  }
  RuntimeCallbacks* cb = Runtime::Current()->GetRuntimeCallbacks();
  // Figure out what kind of chunks we'll be sending.
  CHECK(what == HPSG_WHAT_MERGED_OBJECTS || what == HPSG_WHAT_DISTINCT_OBJECTS)
      << static_cast<int>(what);

  // First, send a heap start chunk.
  uint8_t heap_id[4];
  Set4BE(&heap_id[0], 1);  // Heap id (bogus; we only have one heap).
  cb->DdmPublishChunk(native ? CHUNK_TYPE("NHST") : CHUNK_TYPE("HPST"),
                      ArrayRef<const uint8_t>(heap_id));
  Thread* self = Thread::Current();
  Locks::mutator_lock_->AssertSharedHeld(self);

  // Send a series of heap segment chunks.
  HeapChunkContext context(what == HPSG_WHAT_MERGED_OBJECTS, native);
  auto bump_pointer_space_visitor = [&](mirror::Object* obj)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_) {
    const size_t size = RoundUp(obj->SizeOf(), kObjectAlignment);
    HeapChunkContext::HeapChunkJavaCallback(
        obj, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(obj) + size), size, &context);
  };
  if (native) {
    UNIMPLEMENTED(WARNING) << "Native heap inspection is not supported";
  } else {
    gc::Heap* heap = Runtime::Current()->GetHeap();
    for (const auto& space : heap->GetContinuousSpaces()) {
      if (space->IsDlMallocSpace()) {
        ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
        // dlmalloc's chunk header is 2 * sizeof(size_t), but if the previous chunk is in use for an
        // allocation then the first sizeof(size_t) may belong to it.
        context.SetChunkOverhead(sizeof(size_t));
        space->AsDlMallocSpace()->Walk(HeapChunkContext::HeapChunkJavaCallback, &context);
      } else if (space->IsRosAllocSpace()) {
        context.SetChunkOverhead(0);
        // Need to acquire the mutator lock before the heap bitmap lock with exclusive access since
        // RosAlloc's internal logic doesn't know to release and reacquire the heap bitmap lock.
        ScopedThreadSuspension sts(self, kSuspended);
        ScopedSuspendAll ssa(__FUNCTION__);
        ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
        space->AsRosAllocSpace()->Walk(HeapChunkContext::HeapChunkJavaCallback, &context);
      } else if (space->IsBumpPointerSpace()) {
        ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
        context.SetChunkOverhead(0);
        space->AsBumpPointerSpace()->Walk(bump_pointer_space_visitor);
        HeapChunkContext::HeapChunkJavaCallback(nullptr, nullptr, 0, &context);
      } else if (space->IsRegionSpace()) {
        heap->IncrementDisableMovingGC(self);
        {
          ScopedThreadSuspension sts(self, kSuspended);
          ScopedSuspendAll ssa(__FUNCTION__);
          ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
          context.SetChunkOverhead(0);
          space->AsRegionSpace()->Walk(bump_pointer_space_visitor);
          HeapChunkContext::HeapChunkJavaCallback(nullptr, nullptr, 0, &context);
        }
        heap->DecrementDisableMovingGC(self);
      } else {
        UNIMPLEMENTED(WARNING) << "Not counting objects in space " << *space;
      }
      context.ResetStartOfNextChunk();
    }
    ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
    // Walk the large objects, these are not in the AllocSpace.
    context.SetChunkOverhead(0);
    heap->GetLargeObjectsSpace()->Walk(HeapChunkContext::HeapChunkJavaCallback, &context);
  }

  // Finally, send a heap end chunk.
  cb->DdmPublishChunk(native ? CHUNK_TYPE("NHEN") : CHUNK_TYPE("HPEN"),
                      ArrayRef<const uint8_t>(heap_id));
}

void Dbg::SetAllocTrackingEnabled(bool enable) {
  gc::AllocRecordObjectMap::SetAllocTrackingEnabled(enable);
}

class StringTable {
 private:
  struct Entry {
    explicit Entry(const char* data_in)
        : data(data_in), hash(ComputeModifiedUtf8Hash(data_in)), index(0) {
    }
    Entry(const Entry& entry) = default;
    Entry(Entry&& entry) = default;

    // Pointer to the actual string data.
    const char* data;

    // The hash of the data.
    const uint32_t hash;

    // The index. This will be filled in on Finish and is not part of the ordering, so mark it
    // mutable.
    mutable uint32_t index;

    bool operator==(const Entry& other) const {
      return strcmp(data, other.data) == 0;
    }
  };
  struct EntryHash {
    size_t operator()(const Entry& entry) const {
      return entry.hash;
    }
  };

 public:
  StringTable() : finished_(false) {
  }

  void Add(const char* str, bool copy_string) {
    DCHECK(!finished_);
    if (UNLIKELY(copy_string)) {
      // Check whether it's already there.
      Entry entry(str);
      if (table_.find(entry) != table_.end()) {
        return;
      }

      // Make a copy.
      size_t str_len = strlen(str);
      char* copy = new char[str_len + 1];
      strlcpy(copy, str, str_len + 1);
      string_backup_.emplace_back(copy);
      str = copy;
    }
    Entry entry(str);
    table_.insert(entry);
  }

  // Update all entries and give them an index. Note that this is likely not the insertion order,
  // as the set will with high likelihood reorder elements. Thus, Add must not be called after
  // Finish, and Finish must be called before IndexOf. In that case, WriteTo will walk in
  // the same order as Finish, and indices will agree. The order invariant, as well as indices,
  // are enforced through debug checks.
  void Finish() {
    DCHECK(!finished_);
    finished_ = true;
    uint32_t index = 0;
    for (auto& entry : table_) {
      entry.index = index;
      ++index;
    }
  }

  size_t IndexOf(const char* s) const {
    DCHECK(finished_);
    Entry entry(s);
    auto it = table_.find(entry);
    if (it == table_.end()) {
      LOG(FATAL) << "IndexOf(\"" << s << "\") failed";
    }
    return it->index;
  }

  size_t Size() const {
    return table_.size();
  }

  void WriteTo(std::vector<uint8_t>& bytes) const {
    DCHECK(finished_);
    uint32_t cur_index = 0;
    for (const auto& entry : table_) {
      DCHECK_EQ(cur_index++, entry.index);

      size_t s_len = CountModifiedUtf8Chars(entry.data);
      std::unique_ptr<uint16_t[]> s_utf16(new uint16_t[s_len]);
      ConvertModifiedUtf8ToUtf16(s_utf16.get(), entry.data);
      AppendUtf16BE(bytes, s_utf16.get(), s_len);
    }
  }

 private:
  std::unordered_set<Entry, EntryHash> table_;
  std::vector<std::unique_ptr<char[]>> string_backup_;

  bool finished_;

  DISALLOW_COPY_AND_ASSIGN(StringTable);
};


static const char* GetMethodSourceFile(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(method != nullptr);
  const char* source_file = method->GetDeclaringClassSourceFile();
  return (source_file != nullptr) ? source_file : "";
}

/*
 * The data we send to DDMS contains everything we have recorded.
 *
 * Message header (all values big-endian):
 * (1b) message header len (to allow future expansion); includes itself
 * (1b) entry header len
 * (1b) stack frame len
 * (2b) number of entries
 * (4b) offset to string table from start of message
 * (2b) number of class name strings
 * (2b) number of method name strings
 * (2b) number of source file name strings
 * For each entry:
 *   (4b) total allocation size
 *   (2b) thread id
 *   (2b) allocated object's class name index
 *   (1b) stack depth
 *   For each stack frame:
 *     (2b) method's class name
 *     (2b) method name
 *     (2b) method source file
 *     (2b) line number, clipped to 32767; -2 if native; -1 if no source
 * (xb) class name strings
 * (xb) method name strings
 * (xb) source file strings
 *
 * As with other DDM traffic, strings are sent as a 4-byte length
 * followed by UTF-16 data.
 *
 * We send up 16-bit unsigned indexes into string tables.  In theory there
 * can be (kMaxAllocRecordStackDepth * alloc_record_max_) unique strings in
 * each table, but in practice there should be far fewer.
 *
 * The chief reason for using a string table here is to keep the size of
 * the DDMS message to a minimum.  This is partly to make the protocol
 * efficient, but also because we have to form the whole thing up all at
 * once in a memory buffer.
 *
 * We use separate string tables for class names, method names, and source
 * files to keep the indexes small.  There will generally be no overlap
 * between the contents of these tables.
 */
jbyteArray Dbg::GetRecentAllocations() {
  if ((false)) {
    DumpRecentAllocations();
  }

  Thread* self = Thread::Current();
  std::vector<uint8_t> bytes;
  {
    MutexLock mu(self, *Locks::alloc_tracker_lock_);
    gc::AllocRecordObjectMap* records = Runtime::Current()->GetHeap()->GetAllocationRecords();
    // In case this method is called when allocation tracker is disabled,
    // we should still send some data back.
    gc::AllocRecordObjectMap dummy;
    if (records == nullptr) {
      CHECK(!Runtime::Current()->GetHeap()->IsAllocTrackingEnabled());
      records = &dummy;
    }
    // We don't need to wait on the condition variable records->new_record_condition_, because this
    // function only reads the class objects, which are already marked so it doesn't change their
    // reachability.

    //
    // Part 1: generate string tables.
    //
    StringTable class_names;
    StringTable method_names;
    StringTable filenames;

    VLOG(jdwp) << "Collecting StringTables.";

    const uint16_t capped_count = CappedAllocRecordCount(records->GetRecentAllocationSize());
    uint16_t count = capped_count;
    size_t alloc_byte_count = 0;
    for (auto it = records->RBegin(), end = records->REnd();
         count > 0 && it != end; count--, it++) {
      const gc::AllocRecord* record = &it->second;
      std::string temp;
      const char* class_descr = record->GetClassDescriptor(&temp);
      class_names.Add(class_descr, !temp.empty());

      // Size + tid + class name index + stack depth.
      alloc_byte_count += 4u + 2u + 2u + 1u;

      for (size_t i = 0, depth = record->GetDepth(); i < depth; i++) {
        ArtMethod* m = record->StackElement(i).GetMethod();
        class_names.Add(m->GetDeclaringClassDescriptor(), false);
        method_names.Add(m->GetName(), false);
        filenames.Add(GetMethodSourceFile(m), false);
      }

      // Depth * (class index + method name index + file name index + line number).
      alloc_byte_count += record->GetDepth() * (2u + 2u + 2u + 2u);
    }

    class_names.Finish();
    method_names.Finish();
    filenames.Finish();
    VLOG(jdwp) << "Done collecting StringTables:" << std::endl
               << "  ClassNames: " << class_names.Size() << std::endl
               << "  MethodNames: " << method_names.Size() << std::endl
               << "  Filenames: " << filenames.Size();

    LOG(INFO) << "recent allocation records: " << capped_count;
    LOG(INFO) << "allocation records all objects: " << records->Size();

    //
    // Part 2: Generate the output and store it in the buffer.
    //

    // (1b) message header len (to allow future expansion); includes itself
    // (1b) entry header len
    // (1b) stack frame len
    const int kMessageHeaderLen = 15;
    const int kEntryHeaderLen = 9;
    const int kStackFrameLen = 8;
    Append1BE(bytes, kMessageHeaderLen);
    Append1BE(bytes, kEntryHeaderLen);
    Append1BE(bytes, kStackFrameLen);

    // (2b) number of entries
    // (4b) offset to string table from start of message
    // (2b) number of class name strings
    // (2b) number of method name strings
    // (2b) number of source file name strings
    Append2BE(bytes, capped_count);
    size_t string_table_offset = bytes.size();
    Append4BE(bytes, 0);  // We'll patch this later...
    Append2BE(bytes, class_names.Size());
    Append2BE(bytes, method_names.Size());
    Append2BE(bytes, filenames.Size());

    VLOG(jdwp) << "Dumping allocations with stacks";

    // Enlarge the vector for the allocation data.
    size_t reserve_size = bytes.size() + alloc_byte_count;
    bytes.reserve(reserve_size);

    std::string temp;
    count = capped_count;
    // The last "count" number of allocation records in "records" are the most recent "count" number
    // of allocations. Reverse iterate to get them. The most recent allocation is sent first.
    for (auto it = records->RBegin(), end = records->REnd();
         count > 0 && it != end; count--, it++) {
      // For each entry:
      // (4b) total allocation size
      // (2b) thread id
      // (2b) allocated object's class name index
      // (1b) stack depth
      const gc::AllocRecord* record = &it->second;
      size_t stack_depth = record->GetDepth();
      size_t allocated_object_class_name_index =
          class_names.IndexOf(record->GetClassDescriptor(&temp));
      Append4BE(bytes, record->ByteCount());
      Append2BE(bytes, static_cast<uint16_t>(record->GetTid()));
      Append2BE(bytes, allocated_object_class_name_index);
      Append1BE(bytes, stack_depth);

      for (size_t stack_frame = 0; stack_frame < stack_depth; ++stack_frame) {
        // For each stack frame:
        // (2b) method's class name
        // (2b) method name
        // (2b) method source file
        // (2b) line number, clipped to 32767; -2 if native; -1 if no source
        ArtMethod* m = record->StackElement(stack_frame).GetMethod();
        size_t class_name_index = class_names.IndexOf(m->GetDeclaringClassDescriptor());
        size_t method_name_index = method_names.IndexOf(m->GetName());
        size_t file_name_index = filenames.IndexOf(GetMethodSourceFile(m));
        Append2BE(bytes, class_name_index);
        Append2BE(bytes, method_name_index);
        Append2BE(bytes, file_name_index);
        Append2BE(bytes, record->StackElement(stack_frame).ComputeLineNumber());
      }
    }

    CHECK_EQ(bytes.size(), reserve_size);
    VLOG(jdwp) << "Dumping tables.";

    // (xb) class name strings
    // (xb) method name strings
    // (xb) source file strings
    Set4BE(&bytes[string_table_offset], bytes.size());
    class_names.WriteTo(bytes);
    method_names.WriteTo(bytes);
    filenames.WriteTo(bytes);

    VLOG(jdwp) << "GetRecentAllocations: data created. " << bytes.size();
  }
  JNIEnv* env = self->GetJniEnv();
  jbyteArray result = env->NewByteArray(bytes.size());
  if (result != nullptr) {
    env->SetByteArrayRegion(result, 0, bytes.size(), reinterpret_cast<const jbyte*>(&bytes[0]));
  }
  return result;
}

void Dbg::DbgThreadLifecycleCallback::ThreadStart(Thread* self) {
  Dbg::PostThreadStart(self);
}

void Dbg::DbgThreadLifecycleCallback::ThreadDeath(Thread* self) {
  Dbg::PostThreadDeath(self);
}

}  // namespace art
