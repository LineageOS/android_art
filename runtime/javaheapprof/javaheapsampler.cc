/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "base/atomic.h"
#include "base/locks.h"
#include "gc/heap.h"
#include "javaheapprof/javaheapsampler.h"
#include "runtime.h"

namespace art {

size_t HeapSampler::NextGeoDistRandSample() {
  // Make sure that rng_ and geo_dist are thread safe by acquiring a lock to access.
  art::MutexLock mu(art::Thread::Current(), geo_dist_rng_lock_);
  size_t nsample = geo_dist_(rng_);
  if (nsample == 0) {
    // Geometric distribution results in +ve values but could have zero.
    // In the zero case, return 1.
    nsample = 1;
  }
  return nsample;
}

size_t HeapSampler::PickAndAdjustNextSample(size_t sample_adjust_bytes) {
  size_t bytes_until_sample;
  if (GetSamplingInterval() == 1) {
    bytes_until_sample = 1;
    return bytes_until_sample;
  }
  bytes_until_sample = NextGeoDistRandSample();
  VLOG(heap) << "JHP:PickAndAdjustNextSample, sample_adjust_bytes: "
             << sample_adjust_bytes
             << " bytes_until_sample: " << bytes_until_sample;
  // Adjust the sample bytes
  if (sample_adjust_bytes > 0 && bytes_until_sample > sample_adjust_bytes) {
    bytes_until_sample -= sample_adjust_bytes;
    VLOG(heap) << "JHP:PickAndAdjustNextSample, final bytes_until_sample: "
               << bytes_until_sample;
  }
  return bytes_until_sample;
}

// Report to Perfetto an allocation sample.
// Samples can only be reported after the allocation is done.
// Also bytes_until_sample can only be updated after the allocation and reporting is done.
// Thus next bytes_until_sample is previously calculated (before allocation) to be able to
// get the next tlab_size, but only saved/updated here.
void HeapSampler::ReportSample(art::mirror::Object* obj ATTRIBUTE_UNUSED, size_t allocation_size) {
  VLOG(heap) << "JHP:***Report Perfetto Allocation: alloc_size: " << allocation_size;
}

// Check whether we should take a sample or not at this allocation and calculate the sample
// offset to use in the expand Tlab calculation. Thus the offset from current pos to the next
// sample.
// tlab_used = pos - start
size_t HeapSampler::GetSampleOffset(size_t alloc_size,
                                    size_t tlab_used,
                                    bool* take_sample,
                                    size_t* temp_bytes_until_sample) {
  size_t exhausted_size = alloc_size + tlab_used;
  VLOG(heap) << "JHP:GetSampleOffset: exhausted_size = " << exhausted_size;
  // Note bytes_until_sample is used as an offset from the start point
  size_t bytes_until_sample = *GetBytesUntilSample();
  ssize_t diff = bytes_until_sample - exhausted_size;
  VLOG(heap) << "JHP:GetSampleOffset: diff = " << diff << " bytes_until_sample = "
             << bytes_until_sample;
  if (diff <= 0) {
    *take_sample = true;
    // Compute a new bytes_until_sample
    size_t sample_adj_bytes = -diff;
    size_t next_bytes_until_sample = PickAndAdjustNextSample(sample_adj_bytes);
    VLOG(heap) << "JHP:GetSampleOffset: Take sample, next_bytes_until_sample = "
               << next_bytes_until_sample;
    next_bytes_until_sample += tlab_used;
    VLOG(heap) << "JHP:GetSampleOffset:Next sample offset = "
               << (next_bytes_until_sample - tlab_used);
    // This function is called before the actual allocation happens so we cannot update
    // the bytes_until_sample till after the allocation happens, save it to temp which
    // will be saved after the allocation by the calling function.
    *temp_bytes_until_sample = next_bytes_until_sample;
    return (next_bytes_until_sample - tlab_used);
    // original bytes_until_sample, not offseted
  } else {
    *take_sample = false;
    // The following 2 lines are used in the NonTlab case but have no effect on the
    // Tlab case, because we will only use the temp_bytes_until_sample if the
    // take_sample was true (after returning from this function in Tlab case in the
    // SetBytesUntilSample).
    size_t next_bytes_until_sample = bytes_until_sample - alloc_size;
    *temp_bytes_until_sample = next_bytes_until_sample;
    VLOG(heap) << "JHP:GetSampleOffset: No sample, next_bytes_until_sample= "
               << next_bytes_until_sample << " alloc= " << alloc_size;
    return diff;
  }
}

// We are tracking the location of samples from the start location of the Tlab
// We need to adjust how to calculate the sample position in cases where ResetTlab.
// Adjustment is the new reference position adjustment, usually the new pos-start.
void HeapSampler::AdjustSampleOffset(size_t adjustment) {
  size_t* bytes_until_sample = GetBytesUntilSample();
  size_t cur_bytes_until_sample = *bytes_until_sample;
  if (cur_bytes_until_sample < adjustment) {
    VLOG(heap) << "JHP:AdjustSampleOffset:No Adjustment";
    return;
  }
  size_t next_bytes_until_sample = cur_bytes_until_sample - adjustment;
  *bytes_until_sample = next_bytes_until_sample;
  VLOG(heap) << "JHP:AdjustSampleOffset: adjustment = " << adjustment
             << " next_bytes_until_sample = " << next_bytes_until_sample;
}

// Enable the heap sampler and initialize/set the sampling interval.
void HeapSampler::EnableHeapSampler(void* enable_ptr ATTRIBUTE_UNUSED,
                                    const void* enable_info_ptr ATTRIBUTE_UNUSED) {
  uint64_t interval = 4 * 1024;
  // Set the ART profiler sampling interval to the value from AHeapProfileSessionInfo
  // Set interval to sampling interval from AHeapProfileSessionInfo
  if (interval > 0) {
    // Make sure that rng_ and geo_dist are thread safe by acquiring a lock to access.
    art::MutexLock mu(art::Thread::Current(), geo_dist_rng_lock_);
    SetSamplingInterval(interval);
  }
  // Else default is 4K sampling interval. However, default case shouldn't happen for Perfetto API.
  // AHeapProfileEnableCallbackInfo_getSamplingInterval should always give the requested
  // (non-negative) sampling interval. It is a uint64_t and gets checked for != 0
  // Do not call heap->GetPerfettoJavaHeapProfHeapID() as a temp here, it will build but test run
  // will silently fail. Heap is not fully constructed yet.
  // heap_id will be set through the Perfetto API.
  perfetto_heap_id_ = 1;  // To be set by Perfetto API
  enabled_.store(true, std::memory_order_release);
}

bool HeapSampler::IsEnabled() {
  return enabled_.load(std::memory_order_acquire);
}

void HeapSampler::DisableHeapSampler(void* disable_ptr ATTRIBUTE_UNUSED,
                                     const void* disable_info_ptr ATTRIBUTE_UNUSED) {
  enabled_.store(false, std::memory_order_release);
}

int HeapSampler::GetSamplingInterval() {
  return p_sampling_interval_.load(std::memory_order_acquire);
}

void HeapSampler::SetSamplingInterval(int sampling_interval) {
  p_sampling_interval_.store(sampling_interval, std::memory_order_release);
  geo_dist_.param(std::geometric_distribution<size_t>::param_type(1.0/p_sampling_interval_));
}

void HeapSampler::SetSessionInfo(void* info) {
  perfetto_session_info_ = info;
}

void* HeapSampler::GetSessionInfo() {
  return perfetto_session_info_;
}

}  // namespace art
