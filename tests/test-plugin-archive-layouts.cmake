cmake_minimum_required(VERSION 3.21)

get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET "0123456789abcdef" _nonce)
set(_scratch "${CMAKE_CURRENT_BINARY_DIR}/archive layout test ${_nonce}")
set(_plugin "streamn-obs-scoreboard")
set(_version "1.2.3")
set(_manifest "${_scratch}/source manifest.json")
set(_archives "${_scratch}/archives")
file(MAKE_DIRECTORY "${_scratch}" "${_archives}")
file(WRITE "${_manifest}" "{\"name\":\"${_plugin}\",\"version\":\"${_version}\"}\n")
file(SHA256 "${_manifest}" _manifest_hash)

foreach(_platform IN ITEMS windows linux)
  if(_platform STREQUAL "windows")
    set(_binary_name "${_plugin}.dll")
    set(_archive_suffix "windows-x64.zip")
  else()
    set(_binary_name "${_plugin}.so")
    set(_archive_suffix "linux-x86_64.tar.gz")
  endif()
  set(_binary "${_scratch}/${_binary_name}")
  file(WRITE "${_binary}" "${_platform} binary fixture\n")
  file(SHA256 "${_binary}" _binary_hash)

  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DPLUGIN_BINARY=${_binary}" "-DPLUGIN_MANIFEST=${_manifest}"
            "-DPACKAGE_VERSION=${_version}" "-DPACKAGE_OUTPUT_DIRECTORY=${_archives}"
            -P "${_repo}/cmake/package-plugin-archives.cmake"
    COMMAND_ERROR_IS_FATAL ANY
  )

  foreach(_layout IN ITEMS legacy obs33)
    set(_unpack "${_scratch}/${_platform}-${_layout}")
    file(MAKE_DIRECTORY "${_unpack}")
    if(_layout STREQUAL "legacy")
      set(_archive "${_archives}/${_plugin}-${_version}-${_archive_suffix}")
      set(_binary_path "${_plugin}/bin/64bit/${_binary_name}")
    else()
      set(_archive "${_archives}/${_plugin}-${_version}-obs33-${_archive_suffix}")
      set(_binary_path "${_plugin}/${_binary_name}")
    endif()
    set(_manifest_path "${_plugin}/data/manifest.json")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
      WORKING_DIRECTORY "${_unpack}"
      COMMAND_ERROR_IS_FATAL ANY
    )

    file(GLOB_RECURSE _files RELATIVE "${_unpack}" LIST_DIRECTORIES OFF "${_unpack}/*")
    set(_expected "${_binary_path}" "${_manifest_path}")
    list(SORT _files)
    list(SORT _expected)
    if(NOT _files STREQUAL _expected)
      message(FATAL_ERROR "Unexpected archive contents for ${_platform}/${_layout}: ${_files}")
    endif()
    file(SHA256 "${_unpack}/${_binary_path}" _packaged_binary_hash)
    file(SHA256 "${_unpack}/${_manifest_path}" _packaged_manifest_hash)
    if(NOT _packaged_binary_hash STREQUAL _binary_hash OR
       NOT _packaged_manifest_hash STREQUAL _manifest_hash)
      message(FATAL_ERROR "Packaged files differ from their inputs for ${_platform}/${_layout}.")
    endif()
  endforeach()
endforeach()

file(REMOVE_RECURSE "${_scratch}")
