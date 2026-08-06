# PDE1000: minimal IOEthernetController for Intel 8254x, targeting QEMU's
# e1000-82545em device model. Written from scratch against the public 8254x
# register spec (no Apple e1000 driver source was ever released).
add_kext_bundle(PDE1000.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.openosx.driver.PDE1000 BUNDLE_VERSION 1.0.0)
set_property(TARGET PDE1000.kext PROPERTY OUTPUT_NAME PDE1000)

target_sources(PDE1000.kext PRIVATE
    PDE1000.cpp
)

# PDE1000.cpp includes <IOKit/network/Foo.h>; mirror the same shim trick
# IONetworkingFamily's CMakeLists.txt uses so it can see those headers
# without installing them system-wide.
set(OPENOSX_PDE1000_SHIM_DIR "${CMAKE_CURRENT_BINARY_DIR}/shim-include")
set(OPENOSX_IONF_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../IONetworkingFamily")
file(MAKE_DIRECTORY "${OPENOSX_PDE1000_SHIM_DIR}/IOKit/network")
foreach(hdr
    IOBasicOutputQueue.h IOEthernetController.h IOEthernetInterface.h
    IOEthernetStats.h IOGatedOutputQueue.h IOKernelDebugger.h
    IOMbufMemoryCursor.h IOMbufQueue.h IONetworkController.h
    IONetworkControllerPrivate.h IONetworkData.h IONetworkDebug.h
    IONetworkInterface.h IONetworkMedium.h IONetworkStack.h
    IONetworkStats.h IONetworkTypesPrivate.h IONetworkUserClient.h
    IOOutputQueue.h IOPacketQueue.h)
    file(CREATE_LINK "${OPENOSX_IONF_SRC_DIR}/${hdr}"
        "${OPENOSX_PDE1000_SHIM_DIR}/IOKit/network/${hdr}" SYMBOLIC)
endforeach()

target_include_directories(PDE1000.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENOSX_PDE1000_SHIM_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOPCIFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../xnu/EXTERNAL_HEADERS)
target_link_libraries(PDE1000.kext PRIVATE xnu_kernel_private_headers)
target_compile_definitions(PDE1000.kext PRIVATE __PRIVATE_SPI__)
target_compile_options(PDE1000.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual
    -Wno-deprecated-declarations)
set_property(TARGET PDE1000.kext PROPERTY CXX_STANDARD 14)
add_dependencies(PDE1000.kext IONetworkingFamily.kext)
install(TARGETS PDE1000.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
