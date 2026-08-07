if(NOT OPENOSX_TCC_SOURCE AND DEFINED ENV{OPENOSX_TCC_SOURCE})
    set(OPENOSX_TCC_SOURCE "$ENV{OPENOSX_TCC_SOURCE}")
endif()

if(NOT OPENOSX_TCC_SOURCE)
    message(FATAL_ERROR "OPENOSX_TCC_SOURCE must point to a TinyCC source tree")
endif()

find_program(OPENOSX_MAKE_PROGRAM NAMES gmake make REQUIRED)

set(OPENOSX_TCC_SOURCE_COPY "${CMAKE_CURRENT_BINARY_DIR}/src")
set(OPENOSX_TCC_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/build")
set(OPENOSX_TCC_BIN "${OPENOSX_TCC_BUILD_DIR}/tcc")
set(OPENOSX_TCC_CONFIGURE "${OPENOSX_TCC_SOURCE_COPY}/configure")

set(OPENOSX_TCC_LINKER_FLAG "")
if(OPENOSX_NIX_TOOLCHAIN AND DEFINED ENV{NIX_NATIVE_LD_PATH})
    set(OPENOSX_TCC_LINKER_FLAG "-fuse-ld=$ENV{NIX_NATIVE_LD_PATH}")
endif()

set(OPENOSX_TCC_LDFLAGS
    "${OPENOSX_TCC_LINKER_FLAG} -nostdlib -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub> -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:$<TARGET_FILE:libdyld> -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -Wl,-fixup_chains -Wl,-lSystem"
)

file(GLOB_RECURSE OPENOSX_TCC_SOURCES CONFIGURE_DEPENDS
    "${OPENOSX_TCC_SOURCE}/*"
)

add_custom_command(
    OUTPUT "${OPENOSX_TCC_BIN}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OPENOSX_TCC_SOURCE_COPY}" "${OPENOSX_TCC_BUILD_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OPENOSX_TCC_SOURCE}" "${OPENOSX_TCC_SOURCE_COPY}"
    COMMAND "${CMAKE_COMMAND}" -D "OPENOSX_TCC_SOURCE_COPY=${OPENOSX_TCC_SOURCE_COPY}" -P "${CMAKE_CURRENT_SOURCE_DIR}/patch-tcc.cmake"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPENOSX_TCC_BUILD_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E chdir "${OPENOSX_TCC_BUILD_DIR}"
            "${CMAKE_COMMAND}" -E env
            CC=${CMAKE_C_COMPILER}
            AR=${CMAKE_AR}
            CFLAGS=-Wall\ -O2\ -I${OPENOSX_TCC_BUILD_DIR}\ -I${OPENOSX_TCC_SOURCE_COPY}\ -isysroot\ ${OPENOSX_MACOSX_SDK}
            LDFLAGS=${OPENOSX_TCC_LDFLAGS}
            "${OPENOSX_TCC_CONFIGURE}"
                --source-path=${OPENOSX_TCC_SOURCE_COPY}
            --prefix=/usr
            --bindir=/bin
            --tccdir=/usr/lib/tcc
            --targetos=Darwin
            --cpu=x86_64
            --sysroot=/
            --sysincludepaths=/usr/lib/tcc/include:/usr/include
            --libpaths=/usr/lib/tcc:/usr/lib:/usr/lib/system
            --crtprefix=/usr/lib
            --config-codesign=no
            --config-ldl=no
            --config-pthread=no
            --config-predefs=no
            --config-new_macho=yes
        COMMAND "${OPENOSX_MAKE_PROGRAM}"
            -C "${OPENOSX_TCC_BUILD_DIR}"
            -f "${OPENOSX_TCC_SOURCE_COPY}/Makefile"
            TOP=${OPENOSX_TCC_BUILD_DIR}
            CC=${CMAKE_C_COMPILER}
            AR=${CMAKE_AR}
            LIBS=
            tcc
        DEPENDS
            "$<TARGET_FILE:libSystem_B_stub>"
            "$<TARGET_FILE:libdyld>"
            "${CMAKE_CURRENT_SOURCE_DIR}/patch-tcc.cmake"
            ${OPENOSX_TCC_SOURCES}
        VERBATIM
    )

# tcc needs runtime support files under /usr/lib/tcc: its private headers
# (tccdefs.h and friends, since --config-predefs=no) and libtcc1.a. libtcc1.a
# must be compiled BY tcc itself (its linker consumes tcc's ELF objects, even
# when emitting Mach-O), and the cross tcc above can't run on the build host.
# tinycc's Makefile supports exactly this: a native-host build's
# `x86_64-osx-libtcc1.a` target first builds a host-runnable x86_64-osx-tcc
# cross compiler, then uses it to compile lib/*.c.
set(OPENOSX_TCC_NATIVE_DIR "${CMAKE_CURRENT_BINARY_DIR}/build-native")
set(OPENOSX_TCC_LIBTCC1 "${OPENOSX_TCC_NATIVE_DIR}/x86_64-osx-libtcc1.a")

add_custom_command(
    OUTPUT "${OPENOSX_TCC_LIBTCC1}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OPENOSX_TCC_NATIVE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPENOSX_TCC_NATIVE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${OPENOSX_TCC_NATIVE_DIR}"
        "${CMAKE_COMMAND}" -E env CC=clang --unset=CFLAGS --unset=LDFLAGS
        "${OPENOSX_TCC_CONFIGURE}"
            --source-path=${OPENOSX_TCC_SOURCE_COPY}
    # No TOP= here: the Makefile only defines its build rules when TOP is
    # unset at parse time (TOP set means "included from a sub-make").
    COMMAND "${OPENOSX_MAKE_PROGRAM}"
        -C "${OPENOSX_TCC_NATIVE_DIR}"
        -f "${OPENOSX_TCC_SOURCE_COPY}/Makefile"
        CC=clang
        x86_64-osx-libtcc1.a
    # runmain.o (tcc -run support) is an EXTRA_O the Makefile only builds for
    # native compilers; produce it with the freshly built cross tcc.
    COMMAND "${OPENOSX_TCC_NATIVE_DIR}/x86_64-osx-tcc"
        -c "${OPENOSX_TCC_SOURCE_COPY}/lib/runmain.c"
        -o "${OPENOSX_TCC_NATIVE_DIR}/runmain.o"
        -B "${OPENOSX_TCC_SOURCE_COPY}"
    DEPENDS "${OPENOSX_TCC_BIN}"
    VERBATIM
)

add_custom_target(tcc ALL DEPENDS "${OPENOSX_TCC_BIN}" "${OPENOSX_TCC_LIBTCC1}")
add_dependencies(tcc libSystem_B_stub dyld)

install(PROGRAMS "${OPENOSX_TCC_BIN}" DESTINATION bin COMPONENT BaseSystem)
install(FILES "${OPENOSX_TCC_LIBTCC1}" DESTINATION usr/lib/tcc
    RENAME libtcc1.a COMPONENT BaseSystem)
install(DIRECTORY "${OPENOSX_TCC_SOURCE_COPY}/include/"
    DESTINATION usr/lib/tcc/include COMPONENT BaseSystem
    FILES_MATCHING PATTERN "*.h")
