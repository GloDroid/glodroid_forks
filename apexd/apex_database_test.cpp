/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <string>

#include <android-base/macros.h>
#include <gtest/gtest.h>

#include "apex_database.h"

namespace android {
namespace apex {
namespace {

using MountedApexData = MountedApexDatabase::MountedApexData;

TEST(MountedApexDataTest, LinearOrder) {
  constexpr const char* kLoopName[] = {"loop1", "loop1", "loop2",
                                       "loop2", "loop3", "loop3"};
  constexpr const char* kPath[] = {"path1", "path2", "path1",
                                   "path2", "path1", "path3"};
  constexpr size_t kCount = arraysize(kLoopName);

  MountedApexData data[kCount];
  for (size_t i = 0; i < kCount; ++i) {
    data[i] = MountedApexData(kLoopName[i], kPath[i]);
  }

  for (size_t i = 0; i < kCount; ++i) {
    for (size_t j = i; j < kCount; ++j) {
      if (i != j) {
        EXPECT_TRUE(data[i] < data[j]) << i << " < " << j;
      }
      EXPECT_FALSE(data[j] < data[i]) << "! " << j << " < " << i;
    }
  }
}

size_t CountPackages(const MountedApexDatabase& db) {
  size_t ret = 0;
  db.ForallMountedApexes([&ret](const std::string& a ATTRIBUTE_UNUSED,
                                const MountedApexData& b ATTRIBUTE_UNUSED,
                                bool c ATTRIBUTE_UNUSED) { ++ret; });
  return ret;
}

bool Contains(const MountedApexDatabase& db, const std::string& package,
              const std::string& loop_name, const std::string& full_path) {
  bool found = false;
  db.ForallMountedApexes([&](const std::string& p, const MountedApexData& d,
                             bool b ATTRIBUTE_UNUSED) {
    if (package == p && loop_name == d.loop_name && full_path == d.full_path) {
      found = true;
    }
  });
  return found;
}

bool ContainsPackage(const MountedApexDatabase& db, const std::string& package,
                     const std::string& loop_name,
                     const std::string& full_path) {
  bool found = false;
  db.ForallMountedApexes(
      package, [&](const MountedApexData& d, bool b ATTRIBUTE_UNUSED) {
        if (loop_name == d.loop_name && full_path == d.full_path) {
          found = true;
        }
      });
  return found;
}

TEST(ApexDatabaseTest, AddRemovedMountedApex) {
  constexpr const char* kPackage = "package";
  constexpr const char* kLoopName = "loop";
  constexpr const char* kPath = "path";

  MountedApexDatabase db;
  ASSERT_EQ(CountPackages(db), 0u);

  db.AddMountedApex(kPackage, false, kLoopName, kPath);
  ASSERT_TRUE(Contains(db, kPackage, kLoopName, kPath));
  ASSERT_TRUE(ContainsPackage(db, kPackage, kLoopName, kPath));

  db.RemoveMountedApex(kPackage, kPath);
  EXPECT_FALSE(Contains(db, kPackage, kLoopName, kPath));
  EXPECT_FALSE(ContainsPackage(db, kPackage, kLoopName, kPath));
}

TEST(ApexDatabaseTest, MountMultiple) {
  constexpr const char* kPackage[] = {"package", "package", "package",
                                      "package"};
  constexpr const char* kLoopName[] = {"loop", "loop", "loop3", "loop4"};
  constexpr const char* kPath[] = {"path", "path2", "path", "path4"};

  MountedApexDatabase db;
  ASSERT_EQ(CountPackages(db), 0u);

  for (size_t i = 0; i < arraysize(kPackage); ++i) {
    db.AddMountedApex(kPackage[i], false, kLoopName[i], kPath[i]);
  }

  ASSERT_EQ(CountPackages(db), 4u);
  for (size_t i = 0; i < arraysize(kPackage); ++i) {
    ASSERT_TRUE(Contains(db, kPackage[i], kLoopName[i], kPath[i]));
    ASSERT_TRUE(ContainsPackage(db, kPackage[i], kLoopName[i], kPath[i]));
  }

  db.RemoveMountedApex(kPackage[0], kPath[0]);
  EXPECT_FALSE(Contains(db, kPackage[0], kLoopName[0], kPath[0]));
  EXPECT_FALSE(ContainsPackage(db, kPackage[0], kLoopName[0], kPath[0]));
  EXPECT_TRUE(Contains(db, kPackage[1], kLoopName[1], kPath[1]));
  EXPECT_TRUE(ContainsPackage(db, kPackage[1], kLoopName[1], kPath[1]));
  EXPECT_TRUE(Contains(db, kPackage[2], kLoopName[2], kPath[2]));
  EXPECT_TRUE(ContainsPackage(db, kPackage[2], kLoopName[2], kPath[2]));
  EXPECT_TRUE(Contains(db, kPackage[3], kLoopName[3], kPath[3]));
  EXPECT_TRUE(ContainsPackage(db, kPackage[3], kLoopName[3], kPath[3]));
}

}  // namespace
}  // namespace apex
}  // namespace android

int main(int argc, char** argv) {
  android::base::InitLogging(argv, &android::base::StderrLogger);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
