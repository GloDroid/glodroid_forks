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

#ifndef ANDROID_APEXD_STRING_LOG_H_
#define ANDROID_APEXD_STRING_LOG_H_

// Simple helper class to create strings similar to LOGs.
// Usage sample:
//   std::string msg = StringLog() << "Hello " << std::hex << 1234;

#include <iomanip>
#include <iostream>
#include <sstream>

class StringLog {
 public:
  StringLog() {}

  // Pipe in values.
  template<class T>
  StringLog& operator<<(const T& t) {
    os_stream << t;
    return *this;
  }

  // Pipe in modifiers.
  StringLog& operator<<(std::ostream& (*f)(std::ostream&)) {
    os_stream << f;
    return *this;
  }

  // Get the current string.
  operator std::string() const {
    return os_stream.str();
  }

 private:
  std::ostringstream os_stream;
};

#endif  // ANDROID_APEXD_STRING_LOG_H_
