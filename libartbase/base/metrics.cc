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

#include "metrics.h"

#include "android-base/logging.h"
#include "base/macros.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

std::string DatumName(DatumId datum) {
  switch (datum) {
#define ART_COUNTER(name) \
  case DatumId::k##name:  \
    return #name;
    ART_COUNTERS(ART_COUNTER)
#undef ART_COUNTER
    default:
      LOG(FATAL) << "Unknown datum id: " << static_cast<unsigned>(datum);
      UNREACHABLE();
  }
}

ArtMetrics::ArtMetrics()
    :
#define ART_COUNTER(name) name{},
      ART_COUNTERS(ART_COUNTER)
#undef ART_COUNTER
          unused_{} {
}

void ArtMetrics::ReportAllMetrics(MetricsBackend* backend) const {
// Dump counters
#define ART_COUNTER(name) backend->ReportCounter(DatumId::k##name, name.Value());
  ART_COUNTERS(ART_COUNTER)
#undef ART_COUNTERS
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
