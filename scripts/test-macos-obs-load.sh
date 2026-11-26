#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <OBS.app> <plugin binary> <expected module API version>" >&2
  exit 2
fi

frameworks=$(cd "$1/Contents/Frameworks" && pwd)
plugin=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
expected_api=$3

# Require the module to find OBS and Qt in the launching host, not the SDK.
otool -L "$plugin" | awk '
  /^[[:space:]]/ && $1 !~ /^(@rpath\/|\/usr\/lib\/|\/System\/Library\/)/ {
    print "Non-portable dependency: " $1 > "/dev/stderr"; failed = 1
  }
  END { exit failed }'
runpath=$(otool -l "$plugin" | awk '
  /cmd LC_RPATH$/ { rpath = 1 }
  rpath && $1 == "path" { print $2; rpath = 0 }')
if [[ "$runpath" != '@executable_path/../Frameworks' ]]; then
  echo "Unexpected module runpath: $runpath" >&2
  exit 1
fi

scratch=$(mktemp -d "${TMPDIR:-/tmp}/scoreboard-obs-load.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/Contents/MacOS"
ln -s "$frameworks" "$scratch/Contents/Frameworks"

# Resolve the module and check OBS's required exports without calling the dock initializer.
xcrun clang -std=c11 -Wall -Wextra -Werror -D_DARWIN_C_SOURCE -mmacosx-version-min=13.0 \
  -x c -o "$scratch/Contents/MacOS/check-module" - <<'C'
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL | RTLD_FIRST);
	if (!module) {
		fprintf(stderr, "Cannot load OBS module: %s\n", dlerror());
		return 1;
	}
	uint32_t (*module_ver)(void) = (uint32_t (*)(void))dlsym(module, "obs_module_ver");
	if (!module_ver || !dlsym(module, "obs_module_load") || !dlsym(module, "obs_module_set_pointer")) {
		fprintf(stderr, "Missing required OBS module export\n");
		return 1;
	}
	uint32_t api = module_ver();
	printf("%u.%u.%u\n", api >> 24, (api >> 16) & 0xff, api & 0xffff);
	return 0;
}
C

actual_api=$(env -u DYLD_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH -u DYLD_FALLBACK_LIBRARY_PATH \
  -u DYLD_FALLBACK_FRAMEWORK_PATH -u DYLD_INSERT_LIBRARIES \
  "$scratch/Contents/MacOS/check-module" "$plugin")
if [[ "$actual_api" != "$expected_api" ]]; then
  echo "Expected module API $expected_api, found $actual_api" >&2
  exit 1
fi
echo "OBS module API $actual_api loads against the supplied host frameworks."
