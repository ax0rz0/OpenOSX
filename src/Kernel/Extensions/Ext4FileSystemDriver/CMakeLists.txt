add_kext_bundle(Ext4FileSystemDriver.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.ravynos.driver.Ext4FileSystemDriver BUNDLE_VERSION 1.0.0)
set_property(TARGET Ext4FileSystemDriver.kext PROPERTY OUTPUT_NAME Ext4FileSystemDriver)

target_sources(Ext4FileSystemDriver.kext PRIVATE
    Ext4FileSystemDriver.cpp
)

target_include_directories(Ext4FileSystemDriver.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOStorageFamily/include)
target_link_libraries(Ext4FileSystemDriver.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(Ext4FileSystemDriver.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual)
set_property(TARGET Ext4FileSystemDriver.kext PROPERTY CXX_STANDARD 14)
install(TARGETS Ext4FileSystemDriver.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
