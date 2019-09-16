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

#include "apex_manifest.h"
#include <android-base/file.h>
#include <android-base/logging.h>
#include "string_log.h"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/type_resolver_util.h>
#include <memory>
#include <string>

using android::base::Error;
using android::base::Result;
using google::protobuf::DescriptorPool;
using google::protobuf::util::NewTypeResolverForDescriptorPool;
using google::protobuf::util::TypeResolver;

namespace android {
namespace apex {
namespace {
const char kTypeUrlPrefix[] = "type.googleapis.com";

std::string GetTypeUrl(const ApexManifest& apex_manifest) {
  const google::protobuf::Descriptor* message = apex_manifest.GetDescriptor();
  return std::string(kTypeUrlPrefix) + "/" + message->full_name();
}

// TODO: JsonStringToMessage is a newly added function in protobuf
// and is not yet available in the android tree. Replace this function with
// https://developers.google.com/protocol-buffers/docs/reference/cpp/
// google.protobuf.util.json_util#JsonStringToMessage.details
// as and when the android tree gets updated
Result<void> JsonToApexManifestMessage(const std::string& content,
                                       ApexManifest* apex_manifest) {
  std::unique_ptr<TypeResolver> resolver(NewTypeResolverForDescriptorPool(
      kTypeUrlPrefix, DescriptorPool::generated_pool()));
  std::string binary;
  auto parse_status = JsonToBinaryString(
      resolver.get(), GetTypeUrl(*apex_manifest), content, &binary);
  if (!parse_status.ok()) {
    return Error() << "Failed to parse APEX Manifest JSON config: "
                   << parse_status.error_message().as_string();
  }

  if (!apex_manifest->ParseFromString(binary)) {
    return Error() << "Unexpected fields in APEX Manifest JSON config";
  }
  return {};
}

}  // namespace

Result<ApexManifest> ParseManifest(const std::string& content) {
  ApexManifest apex_manifest;
  std::string err;
  Result<void> parse_manifest_status =
      JsonToApexManifestMessage(content, &apex_manifest);
  if (!parse_manifest_status) {
    return parse_manifest_status.error();
  }

  // Verifying required fields.
  // name
  if (apex_manifest.name().empty()) {
    return Error() << "Missing required field \"name\" from APEX manifest.";
  }

  // version
  if (apex_manifest.version() == 0) {
    return Error() << "Missing required field \"version\" from APEX manifest.";
  }
  return apex_manifest;
}

std::string GetPackageId(const ApexManifest& apexManifest) {
  return apexManifest.name() + "@" + std::to_string(apexManifest.version());
}

Result<ApexManifest> ReadManifest(const std::string& path) {
  std::string content;
  if (!android::base::ReadFileToString(path, &content)) {
    return Error() << "Failed to read manifest file: " << path;
  }
  return ParseManifest(content);
}

}  // namespace apex
}  // namespace android
