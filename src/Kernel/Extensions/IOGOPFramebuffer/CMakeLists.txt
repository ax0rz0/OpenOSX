# IOGOPFramebuffer: minimal boot-time framebuffer driver (courtesy of ravynOS,
# Zoe Knox -- from IOGraphics/IOGOPFramebuffer). Matches an IOPCIDevice named
# "display" and, on start(), pulls the GOP framebuffer geometry the bootloader
# handed XNU via boot_args (PE_Video) and registers it as the kernel graphics
# console via IOPlatformExpert::setConsoleInfo(). The C++ class subclasses the
# real IOGraphicsFamily IOFramebuffer.
add_kext_bundle(IOGOPFramebuffer.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.ravynos.iokit.IOGOPFramebuffer BUNDLE_VERSION 1.0.0)
set_property(TARGET IOGOPFramebuffer.kext PROPERTY OUTPUT_NAME IOGOPFramebuffer)

target_sources(IOGOPFramebuffer.kext PRIVATE
    IOGOPFramebuffer.cpp
)

target_include_directories(IOGOPFramebuffer.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOGraphicsFamily
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/IOHIDSystem
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOPCIFamily/include)
target_link_libraries(IOGOPFramebuffer.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(IOGOPFramebuffer.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual)
set_property(TARGET IOGOPFramebuffer.kext PROPERTY CXX_STANDARD 14)
install(TARGETS IOGOPFramebuffer.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
