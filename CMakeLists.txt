cmake_minimum_required(VERSION 3.25)
project(OpenSc3k LANGUAGES C CXX)

include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# SDL is a platform dependency, not a reconstructed game module. Keep it inside the executable.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
# Legacy game text is Windows-1252 and other code pages; SDL's built-in iconv knows none of them.
if(NOT WIN32)
  set(SDL_SYSTEM_ICONV ON CACHE BOOL "" FORCE)
endif()
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG 829a65d769d935c4852f8159e964312c0957260a # release-3.4.18
  GIT_PROGRESS FALSE)
FetchContent_MakeAvailable(SDL3)
if(WIN32)
  target_compile_definitions(SDL3-static PRIVATE SDL_DISABLE_WINDOWS_IME)
endif()

# The game atlas contains individual glyphs; shaping and color-emoji engines are unnecessary.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3_ttf
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
  GIT_TAG a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b # release-3.2.2
  GIT_SUBMODULES external/freetype
  GIT_PROGRESS FALSE)
FetchContent_MakeAvailable(SDL3_ttf)

# The original IJG codec remains separate C third-party code; all game C++ shares one unit.
file(GLOB JPEG_SOURCES CONFIGURE_DEPENDS third_party/jpeg6a/*.c)
add_library(sc3_jpeg STATIC ${JPEG_SOURCES})
target_include_directories(sc3_jpeg PUBLIC third_party/jpeg6a)
if(MSVC)
  target_compile_definitions(sc3_jpeg PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/*.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${GAME_SOURCES} scripts/unity_order.txt scripts/make_unity.py)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/make_unity.py"
  "${CMAKE_CURRENT_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET)

if(WIN32)
  enable_language(RC)
  set(SC3_WINDOWS_ICON packaging/windows/app.rc)
endif()
add_executable(sc3u "${CMAKE_CURRENT_BINARY_DIR}/game.cpp" ${SC3_WINDOWS_ICON})
# Windows builds are graphical programs and must not open a console window; configure with -DSC3_CONSOLE=ON to get one
# (useful for reading log output while debugging).
option(SC3_CONSOLE "Link the Windows executable as a console program" OFF)
if(WIN32 AND NOT SC3_CONSOLE)
  if(MSVC)
    target_link_options(sc3u PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
  else()
    target_link_options(sc3u PRIVATE -mwindows)
  endif()
endif()
set_target_properties(sc3u PROPERTIES OUTPUT_NAME OpenSc3k CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
set_target_properties(sc3u PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(sc3u PRIVATE src)
target_compile_definitions(sc3u PRIVATE SC3_STATIC SC3_MONOLITHIC)
target_link_libraries(sc3u PRIVATE SDL3::SDL3-static SDL3_ttf::SDL3_ttf-static sc3_jpeg)
if(MSVC)
  target_compile_definitions(sc3u PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(sc3u PRIVATE /W4 /WX /EHsc /bigobj /Gy /Gw /fp:strict)
  target_link_options(sc3u PRIVATE /OPT:REF "/MAP:${CMAKE_CURRENT_BINARY_DIR}/OpenSc3k.map")
else()
  # Preserve the active rounding mode and original arithmetic boundaries instead of assuming nearest or fusing them.
  target_compile_options(sc3u PRIVATE -Wall -Wextra -Wno-unused-parameter
    # Reconstructed layouts keep original padding fields; MSVC-only pragmas are intentionally ignored here.
    -Wno-unused-private-field -Wno-unknown-pragmas -ffunction-sections -fdata-sections
    -fno-fast-math -frounding-math -ffp-contract=off)
  target_compile_options(sc3_jpeg PRIVATE -fno-fast-math -frounding-math -ffp-contract=off)
  if(APPLE)
    target_link_options(sc3u PRIVATE -Wl,-dead_strip)
    # Release executables drop local symbols; Debug and RelWithDebInfo keep them for debuggers.
    target_link_options(sc3u PRIVATE $<$<CONFIG:Release>:-Wl,-x>)
  else()
    target_link_options(sc3u PRIVATE -Wl,--gc-sections $<$<CONFIG:Release>:-s>)
  endif()
endif()
