#!/usr/bin/env python
#
# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import manifest_schema_pb2
from google.protobuf.json_format import Parse
from google.protobuf.json_format import ParseError

class ApexManifest:
	# Default values
	package_name = ""
	version_number = 0
	pre_install_hook = ""
	def __init__(self, manifest_json):
		self.package_name = manifest_json["name"]
		self.version_number = manifest_json["version"]
		if('pre_install_hook' in manifest_json):
			self.pre_install_hook = manifest_json["pre_install_hook"]

class ApexManifestError(Exception):
	def __init__(self, errmessage):
		# Apex Manifest parse error (extra fields) or if required fields not present
		self.errmessage = errmessage

def ValidateApexManifest(manifest_raw):
	try:
		manifest_json = json.loads(manifest_raw)
		# TODO: The version of protobuf library present in the Android tree at the time of writing
		# doesn't support the json_name field name. Proto converts underscore field names to
		# camelCase. To use protobuf with "pre_install_hook" field name, converting to camelCase
		# explicitly. b/121546801
		# Convert field names to camelCase
		for field, value in manifest_json.items():
			manifest_json[to_camel_case(field)] = manifest_json.pop(field)
		manifest_pb = Parse(json.dumps(manifest_json), manifest_schema_pb2.ManifestSchema())
	except (ParseError, ValueError) as err:
		raise ApexManifestError(err)
	# Checking required fields
	if manifest_pb.name == "":
		raise ApexManifestError("'name' field is required.")
	if manifest_pb.version == 0:
		raise ApexManifestError("'version' field is required.")
	return ApexManifest(manifest_json)

def to_camel_case(snake_str):
    components = snake_str.split('_')
    return components[0] + ''.join(x.title() for x in components[1:])