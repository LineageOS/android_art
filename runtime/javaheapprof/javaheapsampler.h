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

#ifndef ART_RUNTIME_JAVAHEAPPROF_JAVAHEAPSAMPLER_H_
#define ART_RUNTIME_JAVAHEAPPROF_JAVAHEAPSAMPLER_H_

#include <random>
#include "base/locks.h"
#include "base/mutex.h"
#include "mirror/object.h"

namespace art {

class HeapSampler {
 public:
  HeapSampler() : rng_(/*seed=*/std::minstd_rand::default_seed),
                  geo_dist_(1.0 / /*expected value=4KB*/ 4096),
                  geo_dist_rng_lock_("Heap Sampler RNG Geometric Dist lock",
                                     art::LockLevel::kGenericBottomLock) {}

  // Set the bytes until sample.
  void SetBytesUntilSample(size_t bytes) {
    *GetBytesUntilSample() = bytes;
  }
  // Get the bytes until sample.
  size_t* GetBytesUntilSample() {
    // Initialization should happen only once the first time the function is called.
    // However there will always be a slot allocated for it at thread creation.
    thread_local size_t bytes_until_sample = 0;
    return &bytes_until_sample;
  }
  void SetHeapID(uint32_t heap_id) {
    perfetto_heap_id_ = heap_id;
  }
  void EnableHeapSampler() {
    enabled_.store(true, std::memory_order_release);
  }
  void DisableHeapSampler() {
    enabled_.store(false, std::memory_order_release);
  }
  // Report a sample to Perfetto.
  void ReportSample(art::mirror::Object* obj, size_t allocation_size);
  // Check whether we should take a sample or not at this allocation, and return the
  // number of bytes from current pos to the next sample to use in the expand Tlab
  // calculation.
  // Update state of both take_sample and temp_bytes_until_sample.
  // tlab_used = pos - start
  // Note: we do not update bytes until sample here. It will be saved after the allocation
  // happens. This function can be called before the actual allocation happens.
  size_t GetSampleOffset(size_t alloc_size,
                         size_t tlab_used,
                         bool* take_sample,
                         size_t* temp_bytes_until_sample) REQUIRES(!geo_dist_rng_lock_);
  // Adjust the sample offset value with the adjustment usually (pos - start)
  // of new Tlab after Reset.
  void AdjustSampleOffset(size_t adjustment);
  // Is heap sampler enabled?
  bool IsEnabled();
  // Set the sampling interval.
  void SetSamplingInterval(int sampling_interval) REQUIRES(!geo_dist_rng_lock_);
  // Return the sampling interval.
  int GetSamplingInterval();

 private:
  size_t NextGeoDistRandSample() REQUIRES(!geo_dist_rng_lock_);
  // Choose, save, and return the number of bytes until the next sample,
  // possibly decreasing sample intervals by sample_adj_bytes.
  size_t PickAndAdjustNextSample(size_t sample_adj_bytes = 0) REQUIRES(!geo_dist_rng_lock_);

  std::atomic<bool> enabled_;
  // Default sampling interval is 4kb.
  // Writes guarded by geo_dist_rng_lock_.
  std::atomic<int> p_sampling_interval_{4 * 1024};
  uint32_t perfetto_heap_id_ = 0;
  // std random number generator.
  std::minstd_rand rng_ GUARDED_BY(geo_dist_rng_lock_);  // Holds the state
  // std geometric distribution
  std::geometric_distribution</*result_type=*/size_t> geo_dist_ GUARDED_BY(geo_dist_rng_lock_);
  // Multiple threads can access the geometric distribution and the random number
  // generator concurrently and thus geo_dist_rng_lock_ is used for thread safety.
  art::Mutex geo_dist_rng_lock_;
};

}  // namespace art

#endif  // ART_RUNTIME_JAVAHEAPPROF_JAVAHEAPSAMPLER_H_
