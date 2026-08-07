# iig is a host-native tool (it runs at build time, generating sources for
# the cross-compiled kext), like migcom, it must NOT be built with the
# osxcross toolchain (that produces a Mach-O binary CMake then cannot execute
# on the Linux build host). Unlike migcom's in-tree add_executable, iig has
# no cross-toolchain-poisoned fallback target: it is sourced entirely from
# the host environment (nix profile / PATH), matching the seeded-via-package-
# manager pattern.
find_program(IIG_EXECUTABLE iig)
if(NOT IIG_EXECUTABLE)
    message(WARNING "iig not found on PATH, "
        "run `nix profile install` in the iig-tools repo "
        "(https://github.com/PureDarwin/iig-tools, or your local checkout) "
        "before building any target that calls iig()")
endif()

function(iig filename)
    cmake_parse_arguments(IIG "" "HEADER;IMPL;FRAMEWORK_NAME" "" ${ARGN})

    if(NOT IIG_HEADER)
        message(SEND_ERROR "iig(${filename}): HEADER must be specified")
        return()
    endif()

    set(IIG_FLAGS)
    if(IIG_FRAMEWORK_NAME)
        list(APPEND IIG_FLAGS --framework-name ${IIG_FRAMEWORK_NAME})
    endif()

    set(IIG_OUTPUTS ${IIG_HEADER})
    if(IIG_IMPL)
        list(APPEND IIG_FLAGS --impl ${IIG_IMPL})
        list(APPEND IIG_OUTPUTS ${IIG_IMPL})
    endif()

    get_filename_component(filename_abs ${filename} ABSOLUTE)
    add_custom_command(OUTPUT ${IIG_OUTPUTS}
        COMMAND ${IIG_EXECUTABLE} --def ${filename_abs} --header ${IIG_HEADER} ${IIG_FLAGS}
        DEPENDS ${filename_abs}
        COMMENT "iig ${filename}" VERBATIM COMMAND_EXPAND_LISTS
    )
endfunction()
