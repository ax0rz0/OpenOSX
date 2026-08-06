add_darwin_executable(mousemon MACOSX_VERSION_MIN 11.0)

get_target_property(_mousemon_opts mousemon COMPILE_OPTIONS)
if(_mousemon_opts)
    list(REMOVE_ITEM _mousemon_opts -nostdinc)
    set_target_properties(mousemon PROPERTIES COMPILE_OPTIONS "${_mousemon_opts}")
endif()

target_sources(mousemon PRIVATE
    mousemon.c
)

target_compile_options(mousemon PRIVATE
    -isysroot ${OPENOSX_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(mousemon PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(mousemon libSystem_B_stub dyld)

install(TARGETS mousemon DESTINATION bin COMPONENT BaseSystem)
