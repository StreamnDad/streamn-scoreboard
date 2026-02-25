cmake_minimum_required(VERSION 3.21)

project(streamn_obs_scoreboard VERSION 0.2.2 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_WERROR "Treat warnings as errors" OFF)
option(ENABLE_COVERAGE "Enable coverage instrumentation for unit-test targets" OFF)
option(BUILD_PLUGIN_MODULE "Build OBS plugin module target" ON)

set(_obs_include_dirs "")
set(_obs_libraries "")
set(_obs_library_dirs "")
set(_obs_cflags_other "")
set(_obs_ldflags_other "")
set(_extra_include_dirs "")
set(_obs_frontend_include_dirs "")
set(_obs_frontend_api_library "")
set(_qt5_found OFF)
set(_qt6_found OFF)

if(BUILD_PLUGIN_MODULE)
  option(USE_OBS_QT_FRAMEWORKS
    "Link against OBS.app's bundled Qt frameworks instead of system Qt (macOS only)" OFF)

  # --- Qt discovery: prefer Qt6, fall back to Qt5, then macOS framework fallback ---
  if(NOT USE_OBS_QT_FRAMEWORKS)
    find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
    if(Qt6_FOUND)
      set(_qt6_found ON)
    else()
      find_package(Qt5 COMPONENTS Core Gui Widgets QUIET)
      if(Qt5_FOUND)
        set(_qt5_found ON)
      endif()
    endif()
  endif()

  if(NOT _qt6_found AND NOT _qt5_found)
    if(APPLE)
      # macOS fallback: search OBS.app bundled Qt frameworks
    else()
      message(FATAL_ERROR
        "Neither Qt6 nor Qt5 was found. Set CMAKE_PREFIX_PATH to your Qt installation.")
    endif()
  endif()

  if(APPLE)
    set(OBS_FRAMEWORKS_DIR "/Applications/OBS.app/Contents/Frameworks" CACHE PATH
      "Path to OBS.app/Contents/Frameworks (for Qt and frontend API discovery)")
  endif()

  # --- OBS SDK discovery ---
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBOBS QUIET libobs)
  endif()

  if(LIBOBS_FOUND)
    set(_obs_include_dirs ${LIBOBS_INCLUDE_DIRS})
    set(_obs_libraries ${LIBOBS_LIBRARIES})
    set(_obs_library_dirs ${LIBOBS_LIBRARY_DIRS})
    set(_obs_cflags_other ${LIBOBS_CFLAGS_OTHER})
    set(_obs_ldflags_other ${LIBOBS_LDFLAGS_OTHER})
  else()
    set(OBS_INCLUDE_DIR "" CACHE PATH "Path to OBS include directory containing obs-module.h")
    set(OBS_LIBRARY "" CACHE FILEPATH "Path to libobs library file")
    set(OBS_SOURCE_DIR "" CACHE PATH "Path to OBS source directory (contains frontend/api/obs-frontend-api.h)")
    set(OBS_FRONTEND_API_LIBRARY "" CACHE FILEPATH "Path to obs-frontend-api library file")
    set(SIMDE_INCLUDE_DIR "" CACHE PATH "Path containing simde/x86/sse2.h (optional override)")

    if(NOT OBS_INCLUDE_DIR OR NOT OBS_LIBRARY)
      message(FATAL_ERROR
        "libobs was not found via pkg-config. Set OBS_INCLUDE_DIR and OBS_LIBRARY "
        "to a local OBS SDK/build path.")
    endif()

    set(_obs_include_dirs ${OBS_INCLUDE_DIR})
    set(_obs_libraries ${OBS_LIBRARY})

    if(NOT OBS_SOURCE_DIR)
      get_filename_component(_obs_source_guess "${OBS_INCLUDE_DIR}" DIRECTORY)
      set(OBS_SOURCE_DIR "${_obs_source_guess}" CACHE PATH "Path to OBS source directory (for frontend API header discovery)" FORCE)
    endif()

    # OBS >=30 moved the header to UI/obs-frontend-api/
    if(EXISTS "${OBS_SOURCE_DIR}/UI/obs-frontend-api/obs-frontend-api.h")
      list(APPEND _obs_frontend_include_dirs "${OBS_SOURCE_DIR}/UI/obs-frontend-api")
    elseif(EXISTS "${OBS_SOURCE_DIR}/frontend/api/obs-frontend-api.h")
      list(APPEND _obs_frontend_include_dirs "${OBS_SOURCE_DIR}/frontend/api")
    endif()

    # --- OBS frontend API library discovery (platform-gated) ---
    if(OBS_FRONTEND_API_LIBRARY)
      set(_obs_frontend_api_library "${OBS_FRONTEND_API_LIBRARY}")
    elseif(APPLE)
      if(EXISTS "${OBS_FRAMEWORKS_DIR}/obs-frontend-api.dylib")
        set(_obs_frontend_api_library "${OBS_FRAMEWORKS_DIR}/obs-frontend-api.dylib")
      else()
        find_library(_obs_frontend_api_library
          NAMES obs-frontend-api obs-frontend-api.dylib
          PATHS
            ${OBS_FRAMEWORKS_DIR}
            /opt/homebrew/lib
            /usr/local/lib
        )
      endif()
    elseif(WIN32)
      find_library(_obs_frontend_api_library
        NAMES obs-frontend-api
        PATHS
          "${OBS_INCLUDE_DIR}/../../bin/64bit"
          "${OBS_INCLUDE_DIR}/../bin/64bit"
      )
    else()
      if(PkgConfig_FOUND)
        pkg_check_modules(OBS_FRONTEND QUIET obs-frontend-api)
      endif()
      if(OBS_FRONTEND_FOUND)
        set(_obs_frontend_api_library ${OBS_FRONTEND_LIBRARIES})
        list(APPEND _obs_frontend_include_dirs ${OBS_FRONTEND_INCLUDE_DIRS})
        list(APPEND _obs_library_dirs ${OBS_FRONTEND_LIBRARY_DIRS})
      else()
        find_library(_obs_frontend_api_library
          NAMES obs-frontend-api
          PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/local/lib
        )
      endif()
    endif()

    # --- SIMDE discovery (platform-gated) ---
    if(NOT SIMDE_INCLUDE_DIR)
      unset(SIMDE_INCLUDE_DIR CACHE)
      if(APPLE)
        find_path(SIMDE_INCLUDE_DIR simde/x86/sse2.h
          PATHS /opt/homebrew/include /usr/local/include
        )
      elseif(UNIX)
        find_path(SIMDE_INCLUDE_DIR simde/x86/sse2.h
          PATHS /usr/include /usr/local/include
        )
      endif()
    endif()

    if(SIMDE_INCLUDE_DIR)
      list(APPEND _extra_include_dirs ${SIMDE_INCLUDE_DIR})
    elseif(NOT WIN32)
      message(WARNING
        "simde headers were not found. Install simde (e.g. 'brew install simde' / "
        "'apt install libsimde-dev') or set -DSIMDE_INCLUDE_DIR=/path/to/include.")
    endif()
  endif()
endif()

add_library(scoreboard_core STATIC
  src/scoreboard-core.c
)

target_include_directories(scoreboard_core
  PRIVATE
    include
)

if(ENABLE_COVERAGE AND NOT MSVC)
  target_compile_options(scoreboard_core PRIVATE -O0 -g --coverage)
  target_link_options(scoreboard_core PRIVATE --coverage)
endif()

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/plugin-version.h
)

if(BUILD_PLUGIN_MODULE)
  # --- macOS framework fallback (compat symlinks + find_library) ---
  # Compile against Qt headers from Homebrew; link against OBS.app's Qt
  # frameworks.  Prefer Qt6 headers (matching OBS 32's runtime) over Qt5.
  if(NOT _qt6_found AND NOT _qt5_found AND APPLE)
    set(_qt_compat_dir "${CMAKE_CURRENT_BINARY_DIR}/qt-compat")
    file(MAKE_DIRECTORY "${_qt_compat_dir}")
    # Prefer unversioned qt (Qt6 on modern Homebrew), fall back to qt@5
    set(_qt_brew_prefixes
      "/opt/homebrew/opt/qt/lib"
      "/usr/local/opt/qt/lib"
      "/opt/homebrew/opt/qt@5/lib"
      "/usr/local/opt/qt@5/lib"
    )
    set(_qt_header_base "")
    foreach(_prefix ${_qt_brew_prefixes})
      if(EXISTS "${_prefix}/QtCore.framework/Headers")
        set(_qt_header_base "${_prefix}")
        break()
      endif()
    endforeach()
    if(_qt_header_base)
      foreach(_qt_mod QtCore QtGui QtWidgets)
        execute_process(
          COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${_qt_header_base}/${_qt_mod}.framework/Headers"
            "${_qt_compat_dir}/${_qt_mod}"
        )
      endforeach()
    endif()

    find_library(_obs_qt_core_library
      NAMES QtCore
      PATHS ${OBS_FRAMEWORKS_DIR}
      NO_DEFAULT_PATH
    )
    find_library(_obs_qt_gui_library
      NAMES QtGui
      PATHS ${OBS_FRAMEWORKS_DIR}
      NO_DEFAULT_PATH
    )
    find_library(_obs_qt_widgets_library
      NAMES QtWidgets
      PATHS ${OBS_FRAMEWORKS_DIR}
      NO_DEFAULT_PATH
    )

    if(NOT _obs_qt_core_library OR NOT _obs_qt_gui_library OR NOT _obs_qt_widgets_library)
      message(FATAL_ERROR
        "OBS Qt frameworks not found under ${OBS_FRAMEWORKS_DIR}.")
    endif()
  endif()

  if(NOT _obs_frontend_api_library)
    message(FATAL_ERROR
      "obs-frontend-api library not found. Set OBS_SOURCE_DIR/OBS paths so frontend APIs are available.")
  endif()

  if(NOT _obs_frontend_include_dirs)
    message(FATAL_ERROR
      "OBS frontend headers not found. Set OBS_SOURCE_DIR so obs-frontend-api.h is discoverable.")
  endif()

  add_library(streamn_obs_scoreboard MODULE
    src/plugin-main.c
    src/plugin-dock.cpp
  )

  # --- Include directories ---
  if(_qt6_found OR _qt5_found)
    target_include_directories(streamn_obs_scoreboard
      PRIVATE
        include
        ${CMAKE_CURRENT_BINARY_DIR}
    )
  else()
    target_include_directories(streamn_obs_scoreboard
      PRIVATE
        include
        ${_qt_compat_dir}
        ${CMAKE_CURRENT_BINARY_DIR}
    )
  endif()

  target_include_directories(streamn_obs_scoreboard
    SYSTEM PRIVATE
      ${_obs_include_dirs}
      ${_obs_frontend_include_dirs}
      ${_extra_include_dirs}
  )

  target_link_directories(streamn_obs_scoreboard
    PRIVATE
      ${_obs_library_dirs}
  )

  # --- Compiler flags (platform-gated) ---
  if(MSVC)
    target_compile_options(streamn_obs_scoreboard PRIVATE /W4)
  else()
    target_compile_options(streamn_obs_scoreboard PRIVATE
      -Wall -Wextra -Wpedantic
    )
    if(APPLE AND NOT _qt5_found AND NOT _qt6_found)
      target_compile_options(streamn_obs_scoreboard PRIVATE
        -F${OBS_FRAMEWORKS_DIR}
        -F/opt/homebrew/opt/qt/lib
      )
    endif()
  endif()

  if(ENABLE_WERROR)
    if(MSVC)
      target_compile_options(streamn_obs_scoreboard PRIVATE /WX)
    else()
      target_compile_options(streamn_obs_scoreboard PRIVATE -Werror)
    endif()
  endif()

  # --- Link libraries ---
  # On macOS with Qt from find_package, prevent OBS framework paths from
  # polluting compile-time framework search (which shadows Qt headers).
  # Wrap raw framework libraries in IMPORTED targets that only contribute
  # link flags, not -F compile flags.
  if(APPLE AND _qt6_found)
    add_library(_obs_link UNKNOWN IMPORTED)
    set_target_properties(_obs_link PROPERTIES IMPORTED_LOCATION "${_obs_libraries}")
    set_property(TARGET _obs_link PROPERTY IMPORTED_NO_SYSTEM ON)
    add_library(_obs_frontend_link UNKNOWN IMPORTED)
    set_target_properties(_obs_frontend_link PROPERTIES IMPORTED_LOCATION "${_obs_frontend_api_library}")
    set_property(TARGET _obs_frontend_link PROPERTY IMPORTED_NO_SYSTEM ON)
    target_link_libraries(streamn_obs_scoreboard
      PRIVATE
        scoreboard_core
        _obs_link
        _obs_frontend_link
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
  elseif(_qt6_found)
    target_link_libraries(streamn_obs_scoreboard
      PRIVATE
        scoreboard_core
        ${_obs_libraries}
        ${_obs_frontend_api_library}
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
  elseif(APPLE AND _qt5_found)
    add_library(_obs_link UNKNOWN IMPORTED)
    set_target_properties(_obs_link PROPERTIES IMPORTED_LOCATION "${_obs_libraries}")
    set_property(TARGET _obs_link PROPERTY IMPORTED_NO_SYSTEM ON)
    add_library(_obs_frontend_link UNKNOWN IMPORTED)
    set_target_properties(_obs_frontend_link PROPERTIES IMPORTED_LOCATION "${_obs_frontend_api_library}")
    set_property(TARGET _obs_frontend_link PROPERTY IMPORTED_NO_SYSTEM ON)
    target_link_libraries(streamn_obs_scoreboard
      PRIVATE
        scoreboard_core
        _obs_link
        _obs_frontend_link
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
    )
  elseif(_qt5_found)
    target_link_libraries(streamn_obs_scoreboard
      PRIVATE
        scoreboard_core
        ${_obs_libraries}
        ${_obs_frontend_api_library}
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
    )
  else()
    target_link_libraries(streamn_obs_scoreboard
      PRIVATE
        scoreboard_core
        ${_obs_libraries}
        ${_obs_frontend_api_library}
        ${_obs_qt_core_library}
        ${_obs_qt_gui_library}
        ${_obs_qt_widgets_library}
    )
  endif()

  target_compile_definitions(streamn_obs_scoreboard
    PRIVATE
      QT_NO_VERSION_TAGGING
  )

  target_compile_options(streamn_obs_scoreboard
    PRIVATE
      ${_obs_cflags_other}
  )

  target_link_options(streamn_obs_scoreboard
    PRIVATE
      ${_obs_ldflags_other}
  )

  set_target_properties(streamn_obs_scoreboard PROPERTIES
    PREFIX ""
    OUTPUT_NAME "streamn-obs-scoreboard"
  )
endif()

enable_testing()

function(add_core_test test_target test_source)
  add_executable(${test_target}
    ${test_source}
  )

  target_include_directories(${test_target}
    PRIVATE
      include
  )

  target_link_libraries(${test_target}
    PRIVATE
      scoreboard_core
  )

  if(ENABLE_COVERAGE AND NOT MSVC)
    target_compile_options(${test_target} PRIVATE -O0 -g --coverage)
    target_link_options(${test_target} PRIVATE --coverage)
  endif()
endfunction()

add_core_test(scoreboard_core_tests tests/test-scoreboard-core.c)
add_core_test(scoreboard_core_scoring_tests tests/test-scoreboard-core-scoring.c)
add_core_test(scoreboard_core_penalties_tests tests/test-scoreboard-core-penalties.c)
add_core_test(scoreboard_core_persistence_tests tests/test-scoreboard-core-persistence.c)

if(BUILD_PLUGIN_MODULE)
  set(PLUGIN_BINARY_PATH "$<TARGET_FILE:streamn_obs_scoreboard>")
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-plugin-binary.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/check-plugin-binary.cmake
    @ONLY
  )

  add_test(
    NAME plugin-binary-exists
    COMMAND ${CMAKE_COMMAND} -DPLUGIN_BINARY_PATH=${PLUGIN_BINARY_PATH}
            -P ${CMAKE_CURRENT_BINARY_DIR}/check-plugin-binary.cmake
  )
endif()

add_test(
  NAME scoreboard-core-tests
  COMMAND scoreboard_core_tests
)

add_test(
  NAME scoreboard-core-scoring-tests
  COMMAND scoreboard_core_scoring_tests
)

add_test(
  NAME scoreboard-core-penalties-tests
  COMMAND scoreboard_core_penalties_tests
)

add_test(
  NAME scoreboard-core-persistence-tests
  COMMAND scoreboard_core_persistence_tests
)
