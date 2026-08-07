foreach(_puredarwin_libsystem_dir
    corecrypto
    libsystem_kernel
    libc
    libplatform
    libmalloc
    pthread
    libdispatch
    libdyld
    stub
)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_puredarwin_libsystem_dir}/CMakeLists.txt")
        add_subdirectory("${_puredarwin_libsystem_dir}")
    endif()
endforeach()
