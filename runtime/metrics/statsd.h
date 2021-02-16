/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef ART_RUNTIME_METRICS_STATSD_H_
#define ART_RUNTIME_METRICS_STATSD_H_

#include <memory>

namespace art {
namespace metrics {

class MetricsBackend;

// Statsd is only supported on Android
#ifdef __ANDROID__
std::unique_ptr<MetricsBackend> CreateStatsdBackend();
#else
inline std::unique_ptr<MetricsBackend> CreateStatsdBackend() { return nullptr; }
#endif

}  // namespace metrics
}  // namespace art

#endif  // ART_RUNTIME_METRICS_STATSD_H_
