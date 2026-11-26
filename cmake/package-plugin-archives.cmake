cmake_minimum_required(VERSION 3.21)

# Run with -DPLUGIN_BINARY=... -DPLUGIN_MANIFEST=... -DPACKAGE_VERSION=...
# -DPACKAGE_OUTPUT_DIRECTORY=... -P cmake/package-plugin-archives.cmake.
foreach(_arg IN ITEMS PLUGIN_BINARY PLUGIN_MANIFEST PACKAGE_VERSION PACKAGE_OUTPUT_DIRECTORY)
  if(NOT DEFINED ${_arg} OR "${${_arg}}" STREQUAL "")
    message(FATAL_ERROR "Set ${_arg} before running this packaging script.")
  endif()
endforeach()

foreach(_input IN ITEMS PLUGIN_BINARY PLUGIN_MANIFEST)
  if(NOT EXISTS "${${_input}}" OR IS_DIRECTORY "${${_input}}")
    message(FATAL_ERROR "${_input} is not a file: ${${_input}}")
  endif()
endforeach()
if(NOT PACKAGE_VERSION MATCHES "^[A-Za-z0-9._+-]+$")
  message(FATAL_ERROR "PACKAGE_VERSION must be a filename-safe version string.")
endif()

set(_plugin "streamn-obs-scoreboard")
get_filename_component(_binary_name "${PLUGIN_BINARY}" NAME)
if(_binary_name STREQUAL "${_plugin}.dll")
  set(_archive_suffix "windows-x64.zip")
  set(_tar_flags cf)
  set(_archive_format zip)
elseif(_binary_name STREQUAL "${_plugin}.so")
  set(_archive_suffix "linux-x86_64.tar.gz")
  set(_tar_flags czf)
  set(_archive_format gnutar)
else()
  message(FATAL_ERROR "Expected ${_plugin}.dll or ${_plugin}.so, got ${_binary_name}.")
endif()

get_filename_component(_output_dir "${PACKAGE_OUTPUT_DIRECTORY}" ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET "0123456789abcdef" _nonce)
set(_scratch "${_output_dir}/.scoreboard-package-${_nonce}")

foreach(_layout IN ITEMS legacy obs33)
  set(_tree "${_scratch}/${_layout}")
  set(_root "${_tree}/${_plugin}")
  set(_binary_dir "${_root}")
  set(_edition "")
  if(_layout STREQUAL "legacy")
    set(_binary_dir "${_root}/bin/64bit")
  else()
    set(_edition "-obs33")
  endif()

  file(MAKE_DIRECTORY "${_binary_dir}" "${_root}/data")
  configure_file("${PLUGIN_BINARY}" "${_binary_dir}/${_binary_name}" COPYONLY)
  configure_file("${PLUGIN_MANIFEST}" "${_root}/data/manifest.json" COPYONLY)

  set(_archive "${_plugin}-${PACKAGE_VERSION}${_edition}-${_archive_suffix}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar ${_tar_flags} "${_scratch}/${_archive}"
            "--format=${_archive_format}" "${_plugin}"
    WORKING_DIRECTORY "${_tree}"
    RESULT_VARIABLE _status
  )
  if(NOT _status EQUAL 0)
    file(REMOVE_RECURSE "${_scratch}")
    message(FATAL_ERROR "Could not create ${_archive} (status ${_status}).")
  endif()
  list(APPEND _archives "${_archive}")
endforeach()

foreach(_archive IN LISTS _archives)
  file(RENAME "${_scratch}/${_archive}" "${_output_dir}/${_archive}" RESULT _status)
  if(NOT _status STREQUAL "0")
    file(REMOVE_RECURSE "${_scratch}")
    message(FATAL_ERROR "Could not publish ${_archive}: ${_status}")
  endif()
  message(STATUS "Created ${_output_dir}/${_archive}")
endforeach()
file(REMOVE_RECURSE "${_scratch}")
