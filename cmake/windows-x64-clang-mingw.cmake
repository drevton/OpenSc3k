# Cross-compile the Windows x64 executable from macOS or Linux with clang.
#
#   cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/windows-x64-clang-mingw.cmake
#   cmake --build build/windows --target sc3u
#
# clang compiles; the mingw-w64 GCC driver links (it knows the CRT and runtime libraries), statically, so the
# resulting OpenSc3k.exe needs no extra DLLs. Install mingw-w64 first (macOS: brew install mingw-w64) and, if it is
# not in the default Homebrew location, pass -DMINGW_TOOLCHAIN=/path/to/toolchain-x86_64.
if(NOT MINGW_TOOLCHAIN)
  set(MINGW_TOOLCHAIN /opt/homebrew/opt/mingw-w64/toolchain-x86_64 CACHE PATH "mingw-w64 x86_64 toolchain root")
endif()
set(_triple x86_64-w64-mingw32)
set(_sysroot ${MINGW_TOOLCHAIN}/${_triple})
file(GLOB _gxx_includes ${_sysroot}/include/c++/*)
list(GET _gxx_includes 0 _gxx)
set(_wrap ${CMAKE_BINARY_DIR}/clang-mingw)
file(MAKE_DIRECTORY ${_wrap})
file(WRITE ${_wrap}/cxx "#!/bin/sh
for a in \"$@\"; do case \"$a\" in -c|-E|-S) COMPILE=1;; esac; done
if [ -n \"$COMPILE\" ]; then
  exec clang++ --target=${_triple} -fms-extensions -nostdinc++ -isystem ${_gxx} -isystem ${_gxx}/${_triple} \\
    -isystem ${_gxx}/backward -isystem ${_sysroot}/include \"$@\"
else
  exec ${MINGW_TOOLCHAIN}/bin/${_triple}-g++ \"$@\"
fi
")
file(WRITE ${_wrap}/cc "#!/bin/sh
for a in \"$@\"; do case \"$a\" in -c|-E|-S) COMPILE=1;; esac; done
if [ -n \"$COMPILE\" ]; then
  exec clang --target=${_triple} -fms-extensions -isystem ${_sysroot}/include \"$@\"
else
  exec ${MINGW_TOOLCHAIN}/bin/${_triple}-gcc \"$@\"
fi
")
file(CHMOD ${_wrap}/cc ${_wrap}/cxx PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${_wrap}/cc)
set(CMAKE_CXX_COMPILER ${_wrap}/cxx)
set(CMAKE_AR ${MINGW_TOOLCHAIN}/bin/${_triple}-ar)
set(CMAKE_RANLIB ${MINGW_TOOLCHAIN}/bin/${_triple}-ranlib)
set(CMAKE_RC_COMPILER ${MINGW_TOOLCHAIN}/bin/${_triple}-windres)
set(CMAKE_FIND_ROOT_PATH ${_sysroot})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
