add_darwin_executable(startx MACOSX_VERSION_MIN 11.0)

get_target_property(_startx_opts startx COMPILE_OPTIONS)
if(_startx_opts)
    list(REMOVE_ITEM _startx_opts -nostdinc)
    set_target_properties(startx PROPERTIES COMPILE_OPTIONS "${_startx_opts}")
endif()

target_sources(startx PRIVATE
    startx.c
)

target_compile_options(startx PRIVATE
    -isysroot ${OPENOSX_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(startx PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(startx libSystem_B_stub dyld)

install(TARGETS startx DESTINATION bin COMPONENT BaseSystem)
