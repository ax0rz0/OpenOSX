# RavynAHCIPort: minimal AHCI/SATA disk driver (courtesy of ravynOS, Zoe Knox).
# Matches ICH9 AHCI (0x29228086) + the generic AHCI PCI class 0x010601, so it
# probes on QEMU q35 (ich9-ahci). Polling-based, slot-0-only for now; kept arch-
# agnostic (deps IOPCIFamily/IOStorageFamily build unconditionally) so it can
# serve other platforms later.
add_kext_bundle(RavynAHCIPort.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.ravynos.driver.RavynAHCIPort BUNDLE_VERSION 1.0.0)
set_property(TARGET RavynAHCIPort.kext PROPERTY OUTPUT_NAME RavynAHCIPort)

target_sources(RavynAHCIPort.kext PRIVATE
    RavynAHCIPort.cpp
    RavynAHCIDisk.cpp
)

target_include_directories(RavynAHCIPort.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOPCIFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOStorageFamily/include)
target_link_libraries(RavynAHCIPort.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(RavynAHCIPort.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual)
set_property(TARGET RavynAHCIPort.kext PROPERTY CXX_STANDARD 14)
install(TARGETS RavynAHCIPort.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
