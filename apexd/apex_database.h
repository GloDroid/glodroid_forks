/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef ANDROID_APEXD_APEX_DATABASE_H_
#define ANDROID_APEXD_APEX_DATABASE_H_

#include <map>
#include <string>

#include <android-base/logging.h>

namespace android {
namespace apex {

class MountedApexDatabase {
 public:
  // Stores associated low-level data for a mounted APEX. To conserve memory,
  // the APEX file isn't stored, but must be opened to retrieve specific data.
  struct MountedApexData {
    std::string loop_name;  // Loop device used (fs path).
    std::string full_path;  // Full path to the apex file.

    MountedApexData(const std::string& loop_name, const std::string& full_path)
        : loop_name(loop_name), full_path(full_path) {}

    inline bool operator<(const MountedApexData& rhs) const {
      return (loop_name < rhs.loop_name) && (full_path < rhs.full_path);
    }
  };

  inline void CheckAtMostOneLatest() {
    for (const auto& apex_set : mounted_apexes_) {
      size_t count = 0;
      for (const auto& pair : apex_set.second) {
        if (pair.second) {
          count++;
        }
      }
      CHECK_LE(count, 1u) << apex_set.first;
    }
  }

  template <typename... Args>
  inline void AddMountedApex(const std::string& package, bool latest,
                             Args&&... args) {
    auto it = mounted_apexes_.find(package);
    if (it == mounted_apexes_.end()) {
      auto insert_it =
          mounted_apexes_.emplace(package, std::map<MountedApexData, bool>());
      CHECK(insert_it.second);
      it = insert_it.first;
    }

    auto check_it = it->second.emplace(
        MountedApexData(std::forward<Args>(args)...), latest);
    CHECK(check_it.second);

    CheckAtMostOneLatest();
  }

  inline void RemoveMountedApex(const std::string& package,
                                const std::string& full_path) {
    auto it = mounted_apexes_.find(package);
    if (it == mounted_apexes_.end()) {
      return;
    }

    auto& pkg_map = it->second;

    for (auto pkg_it = pkg_map.begin(); pkg_it != pkg_map.end(); ++pkg_it) {
      if (pkg_it->first.full_path == full_path) {
        pkg_map.erase(pkg_it);
        return;
      }
    }
  }

  inline void UnsetLatestForall(const std::string& package) {
    auto it = mounted_apexes_.find(package);
    if (it == mounted_apexes_.end()) {
      return;
    }
    for (auto& data : it->second) {
      data.second = false;
    }
  }

  template <typename T>
  inline void ForallMountedApexes(const std::string& package,
                                  const T& handler) {
    auto it = mounted_apexes_.find(package);
    if (it == mounted_apexes_.end()) {
      return;
    }
    for (auto& pair : it->second) {
      handler(pair.first, pair.second);
    }
  }

  template <typename T>
  inline void ForallMountedApexes(const T& handler) {
    for (const auto& pkg : mounted_apexes_) {
      for (const auto& pair : pkg.second) {
        handler(pkg.first, pair.first, pair.second);
      }
    }
  }

 private:
  // A map from package name to mounted apexes.
  // Note: using std::maps to
  //         a) so we do not have to worry about iterator invalidation.
  //         b) do not have to const_cast (over std::set)
  // TODO: Eventually this structure (and functions) need to be guarded by
  // locks.
  std::map<std::string, std::map<MountedApexData, bool>> mounted_apexes_;
};

}  // namespace apex
}  // namespace android

#endif  // ANDROID_APEXD_APEX_DATABASE_H_
