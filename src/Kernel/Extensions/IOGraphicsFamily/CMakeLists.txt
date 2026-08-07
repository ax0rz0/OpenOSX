add_kext_bundle(IOGraphicsFamily.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    MAIN_FUNCTION IOGraphicsFamilyModuleStart
    BUNDLE_IDENTIFIER com.apple.iokit.IOGraphicsFamily BUNDLE_VERSION 576.1.0)
set_property(TARGET IOGraphicsFamily.kext PROPERTY OUTPUT_NAME IOGraphicsFamily)

target_sources(IOGraphicsFamily.kext PRIVATE
    IOBacklightDisplay.cpp
    IOBootFramebuffer.cpp
    IODisplay.cpp
    IODisplayWrangler.cpp
    IOFramebuffer.cpp
    IOFramebufferUserClient.cpp
    IOGraphicsDevice.cpp
    IOKit/graphics/tl/osmemory.cpp
    IODisplayWranglerUserClients.cpp
    IOI2CInterface.cpp)

target_include_directories(IOGraphicsFamily.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/IOKit
    ${CMAKE_CURRENT_SOURCE_DIR}/IOKit/graphics/tl
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../GTrace/Kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/../GMetric
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOPCIFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOACPIFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IONDRVSupport
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/IOHIDSystem
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/IOHIDSystem/IOKit/hidsystem
    ${CMAKE_CURRENT_SOURCE_DIR}/../../xnu/EXTERNAL_HEADERS)
target_link_libraries(IOGraphicsFamily.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(IOGraphicsFamily.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual
    -Wno-deprecated-declarations)
target_compile_definitions(IOGraphicsFamily.kext PRIVATE
    GTRACE_REVISION=0
    ENABLE_TELEMETRY=0
    ENABLE_IONDRV_TELEMETRY=0
    # Compile in DEBG/D() kprintf tracing; runtime-masked by the iogdebg
    # boot-arg / debug.iogdebg sysctl (default: everything but NOTIFICATIONS).
    DEBG_CATEGORIES_BUILD=0x1ffull)
set_property(TARGET IOGraphicsFamily.kext PROPERTY CXX_STANDARD 14)
install(TARGETS IOGraphicsFamily.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
