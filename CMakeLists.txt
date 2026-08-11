# =============================================================================
# ToughnessFeast — RE_Kenshi plugin
# =============================================================================
# Linux CLion: IDE stubs (edit only).
# Windows MSVC / GitHub Actions: real KenshiLib → ToughnessFeast.dll
#
# Game build options:
#   -DKENSHILIB_DIR=.../KenshiLib
#   -DBOOST_INCLUDE_DIR=.../boost_1_60_0
#   -DTOUGHNESSFEAST_FORCE_GAME_BUILD=ON   (optional; implied by MSVC)
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(ToughnessFeast VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TOUGHNESSFEAST_FORCE_GAME_BUILD
  "Build against real KenshiLib (Windows MSVC). Off = Linux IDE stubs." OFF)

set(_BUNDLED_KENSHI "${CMAKE_SOURCE_DIR}/third_party/KenshiLib")
set(IDE_STUBS_DIR "${CMAKE_SOURCE_DIR}/third_party/kenshi-ide-stubs")
set(WIN32_STUBS_DIR "${CMAKE_SOURCE_DIR}/third_party/win32-stubs")

if(DEFINED ENV{KENSHILIB_DIR} AND NOT "$ENV{KENSHILIB_DIR}" STREQUAL "" AND NOT DEFINED KENSHILIB_DIR)
  set(KENSHILIB_DIR "$ENV{KENSHILIB_DIR}")
endif()
if(NOT DEFINED KENSHILIB_DIR OR KENSHILIB_DIR STREQUAL "")
  if(EXISTS "${_BUNDLED_KENSHI}/Include/Debug.h")
    set(KENSHILIB_DIR "${_BUNDLED_KENSHI}")
  endif()
endif()
if(DEFINED KENSHILIB_DIR)
  file(TO_CMAKE_PATH "${KENSHILIB_DIR}" KENSHILIB_DIR)
endif()

if(DEFINED ENV{BOOST_INCLUDE_DIR} AND NOT DEFINED BOOST_INCLUDE_DIR)
  set(BOOST_INCLUDE_DIR "$ENV{BOOST_INCLUDE_DIR}")
endif()
if(DEFINED ENV{BOOST_INCLUDE_PATH} AND NOT DEFINED BOOST_INCLUDE_DIR)
  set(BOOST_INCLUDE_DIR "$ENV{BOOST_INCLUDE_PATH}")
endif()
if(DEFINED BOOST_INCLUDE_DIR)
  file(TO_CMAKE_PATH "${BOOST_INCLUDE_DIR}" BOOST_INCLUDE_DIR)
endif()

# Game build if MSVC or forced
set(_GAME_BUILD FALSE)
if(MSVC OR TOUGHNESSFEAST_FORCE_GAME_BUILD)
  set(_GAME_BUILD TRUE)
endif()

add_library(ToughnessFeast SHARED src/ToughnessFeast.cpp)

if(_GAME_BUILD)
  message(STATUS "GAME BUILD: real KenshiLib (produce ToughnessFeast.dll)")

  if(NOT KENSHILIB_DIR OR NOT EXISTS "${KENSHILIB_DIR}/Include/Debug.h")
    message(FATAL_ERROR "KENSHILIB_DIR missing Include/Debug.h (got: '${KENSHILIB_DIR}')")
  endif()

  set(KENSHI_INCLUDE_DIR "${KENSHILIB_DIR}/Include")
  set(KENSHI_LIB_DIR "${KENSHILIB_DIR}/Libraries")

  target_include_directories(ToughnessFeast PRIVATE "${KENSHI_INCLUDE_DIR}")
  if(BOOST_INCLUDE_DIR AND EXISTS "${BOOST_INCLUDE_DIR}/boost")
    target_include_directories(ToughnessFeast PRIVATE "${BOOST_INCLUDE_DIR}")
    message(STATUS "Boost includes: ${BOOST_INCLUDE_DIR}")
  else()
    message(WARNING "BOOST_INCLUDE_DIR not set — MedicalSystem headers may fail to find boost/")
  endif()

  target_compile_definitions(ToughnessFeast PRIVATE
    _WINDOWS
    _USRDLL
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<CONFIG:Release>:NDEBUG>
  )

  if(MSVC)
    set_property(TARGET ToughnessFeast PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    # Whole program optimization required by KenshiLib::GetRealAddress for member ptrs
    # (we also use RVA hooks; LTCG still recommended)
    target_compile_options(ToughnessFeast PRIVATE /W3 /EHsc /bigobj /GL /O2)
    target_link_options(ToughnessFeast PRIVATE /LTCG /INCREMENTAL:NO)
    set_property(TARGET ToughnessFeast PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()

  # Link KenshiLib (filename case varies)
  set(_klib "")
  foreach(_name KenshiLib.lib kenshilib.lib KenshiLib kenshilib)
    if(EXISTS "${KENSHI_LIB_DIR}/${_name}" OR EXISTS "${KENSHI_LIB_DIR}/${_name}.lib")
      set(_klib "${_name}")
      break()
    endif()
  endforeach()
  if(EXISTS "${KENSHI_LIB_DIR}/KenshiLib.lib")
    file(SIZE "${KENSHI_LIB_DIR}/KenshiLib.lib" _libsize)
  elseif(EXISTS "${KENSHI_LIB_DIR}/kenshilib.lib")
    file(SIZE "${KENSHI_LIB_DIR}/kenshilib.lib" _libsize)
  else()
    set(_libsize 0)
  endif()
  if(_libsize LESS 10000)
    message(FATAL_ERROR
      "KenshiLib.lib missing or Git-LFS pointer (${_libsize} bytes) in ${KENSHI_LIB_DIR}.\n"
      "Clone https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps with git lfs pull")
  endif()

  target_link_directories(ToughnessFeast PRIVATE "${KENSHI_LIB_DIR}")
  target_link_libraries(ToughnessFeast PRIVATE KenshiLib MyGUIEngine_x64)
  message(STATUS "Linking KenshiLib from ${KENSHI_LIB_DIR} (${_libsize} bytes)")

else()
  message(STATUS "IDE MODE (non-MSVC): third_party/kenshi-ide-stubs — not a game DLL")
  target_include_directories(ToughnessFeast PRIVATE
    "${IDE_STUBS_DIR}"
    "${WIN32_STUBS_DIR}"
  )
  target_compile_definitions(ToughnessFeast PRIVATE TOUGHNESSFEAST_LINUX_IDE=1)
  target_compile_options(ToughnessFeast PRIVATE -Wall -Wno-unknown-pragmas)
endif()

set_target_properties(ToughnessFeast PROPERTIES
  OUTPUT_NAME "ToughnessFeast"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
)

set(MOD_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/kenshi_mod")
if(_GAME_BUILD AND MSVC)
  add_custom_command(TARGET ToughnessFeast POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_OUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:ToughnessFeast>"
      "${MOD_OUT_DIR}/ToughnessFeast.dll"
    COMMENT "Copy DLL → kenshi_mod/"
  )
endif()
