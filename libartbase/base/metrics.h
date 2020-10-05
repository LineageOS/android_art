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

#ifndef ART_LIBARTBASE_BASE_METRICS_H_
#define ART_LIBARTBASE_BASE_METRICS_H_

#include <stdint.h>

#include <array>
#include <ostream>
#include <string_view>

#include "base/time_utils.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

// COUNTER(counter_name)
#define ART_COUNTERS(COUNTER) COUNTER(ClassVerificationTotalTime)
// TODO: ClassVerificationTime serves as a mock for now. Implementation will come later.

namespace art {
namespace metrics {

/**
 * An enumeration of all ART counters and histograms.
 */
enum class DatumId {
#define ART_COUNTER(name) k##name,
  ART_COUNTERS(ART_COUNTER)
#undef ART_COUNTER
};

struct SessionData {
  const uint64_t session_id;
  const std::string_view package_name;
  // TODO: compiler filter / dexopt state
};

// MetricsBackends are used by a metrics reporter to write metrics to some external location. For
// example, a backend might write to logcat, or to a file, or to statsd.
class MetricsBackend {
 public:
  virtual ~MetricsBackend() {}

 protected:
  // Begins an ART metrics session.
  //
  // This is called by the metrics reporter when the runtime is starting up. The session_data
  // includes a session id which is used to correlate any metric reports with the same instance of
  // the ART runtime. Additionally, session_data includes useful metadata such as the package name
  // for this process.
  virtual void BeginSession(const SessionData& session_data) = 0;

  // Marks the end of a metrics session.
  //
  // The metrics reporter will call this when metrics reported ends (e.g. when the runtime is
  // shutting down). No further metrics will be reported for this session. Note that EndSession is
  // not guaranteed to be called, since clean shutdowns for the runtime are quite rare in practice.
  virtual void EndSession() = 0;

  // Called by the metrics reporter to give the current value of the counter with id counter_type.
  //
  // This will be called multiple times for each counter based on when the metrics reporter chooses
  // to report metrics. For example, the metrics reporter may call this at shutdown or every N
  // minutes. Counters are not reset in between invocations, so the value should represent the
  // total count at the point this method is called.
  virtual void ReportCounter(DatumId counter_type, uint64_t value) = 0;

  friend class ArtMetrics;
};

class MetricsCounter {
 public:
  explicit constexpr MetricsCounter(uint64_t value = 0) : value_{value} {}

  void AddOne() { value_++; }
  void Add(uint64_t value) { value_ += value; }

  uint64_t Value() const { return value_; }

 private:
  uint64_t value_;
};

/**
 * This struct contains all of the metrics that ART reports.
 */
class ArtMetrics {
 public:
  ArtMetrics();

  void ReportAllMetrics(MetricsBackend* backend) const;

#define ART_COUNTER(name) MetricsCounter name;
  ART_COUNTERS(ART_COUNTER)
#undef ART_COUNTER

 private:
  // This field is only included to allow us expand the ART_COUNTERS and ART_HISTOGRAMS macro in
  // the initializer list in ArtMetrics::ArtMetrics. See metrics.cc for how it's used.
  //
  // It's declared as a zero-length array so it has no runtime space impact.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
  int unused_[0];
#pragma clang diagnostic pop  // -Wunused-private-field
};

// Returns a human readable name for the given DatumId.
std::string DatumName(DatumId datum);

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion

#endif  // ART_LIBARTBASE_BASE_METRICS_H_
