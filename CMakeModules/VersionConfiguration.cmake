include(BuildManifest)
string(TIMESTAMP CURRENT_DATE "%Y-%m-%d")
option(UPGRADE_DEBUG "" OFF)

set(VERSION_STRING "${JMP_APP_VERSION}")
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" VERSION_STRING_SHORT "${VERSION_STRING}")
set(CANONICAL_VERSION_STRING "${VERSION_STRING_SHORT}")
string(REPLACE "." ";" _version_parts "${VERSION_STRING_SHORT}")
list(GET _version_parts 0 VERSION_MAJOR)
list(GET _version_parts 1 VERSION_MINOR)
list(GET _version_parts 2 VERSION_NANO)

# MSI only compares three integer parts. The package revision is independent
# of semantic application prerelease identifiers and increases on every tag.
set(PACKAGE_VERSION "${JMP_PACKAGE_VERSION}")
if(DEFINED ENV{BUILD_NUMBER})
  set(VERSION_BUILD "$ENV{BUILD_NUMBER}")
else()
  set(VERSION_BUILD "dev")
endif()
configure_file(src/core/Version.cpp.in src/core/Version.cpp @ONLY)
