# RavynXHCIPort: minimal xHCI host controller + USB Mass Storage (bulk-only
# transport) driver, for booting off USB when no SATA/AHCI disk is present.
add_kext_bundle(RavynXHCIPort.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.ravynos.driver.RavynXHCIPort BUNDLE_VERSION 1.0.0)
set_property(TARGET RavynXHCIPort.kext PROPERTY OUTPUT_NAME RavynXHCIPort)

target_sources(RavynXHCIPort.kext PRIVATE
    RavynXHCIPort.cpp
    RavynXHCIMassStorageDisk.cpp
    RavynXHCIUSBBus.cpp
)

target_include_directories(RavynXHCIPort.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOPCIFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOStorageFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/IOHIDSystem
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/include)
target_link_libraries(RavynXHCIPort.kext PRIVATE xnu_kernel_private_headers IOUSBFamilyHeaders)
target_compile_options(RavynXHCIPort.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual)
set_property(TARGET RavynXHCIPort.kext PROPERTY CXX_STANDARD 14)
install(TARGETS RavynXHCIPort.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
