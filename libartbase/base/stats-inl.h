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

#ifndef ART_LIBARTBASE_BASE_STATS_INL_H_
#define ART_LIBARTBASE_BASE_STATS_INL_H_

#include <iomanip>
#include <map>

#include "base/stats.h"
#include "base/indenter.h"

namespace art {
  void Stats::DumpSizes(VariableIndentationOutputStream& os, std::string_view name) const {
    Dump(os, name, Value(), 1000.0, "KB");
  }

  void Stats::Dump(VariableIndentationOutputStream& os,
                   std::string_view name,
                   double total,
                   double unit_size,
                   const char* unit) const {
    double percent = total > 0 ? 100.0 * Value() / total : 0;
    const size_t name_width = 52 - os.GetIndentation();
    if (name.length() > name_width) {
      // Handle very long names by printing them on their own line.
      os.Stream() << name << " \\\n";
      name = "";
    }
    os.Stream()
        << std::setw(name_width) << std::left << name << " "
        << std::setw(6) << std::right << Count() << " "
        << std::setw(10) << std::fixed << std::setprecision(3) << Value() / unit_size << unit << " "
        << std::setw(6) << std::fixed << std::setprecision(1) << percent << "%\n";

    // Sort all children by largest value first, then by name.
    std::map<std::pair<double, std::string_view>, const Stats&> sorted_children;
    for (const auto& it : Children()) {
      sorted_children.emplace(std::make_pair(-it.second.Value(), it.first), it.second);
    }

    // Add "other" row to represent any amount not account for by the children.
    Stats other;
    other.AddBytes(Value() - SumChildrenValues(), Count());
    if (other.Value() != 0.0 && !Children().empty()) {
      sorted_children.emplace(std::make_pair(-other.Value(), "(other)"), other);
    }

    // Print the data.
    ScopedIndentation indent1(&os);
    for (const auto& it : sorted_children) {
      it.second.Dump(os, it.first.second, total, unit_size, unit);
    }
  }
}  // namespace art

#endif  // ART_LIBARTBASE_BASE_STATS_INL_H_
