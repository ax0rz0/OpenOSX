add_kext_bundle(AppleFileSystemDriver.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.apple.AppleFileSystemDriver BUNDLE_VERSION 3.0.1)
set_property(TARGET AppleFileSystemDriver.kext PROPERTY OUTPUT_NAME AppleFileSystemDriver)

target_sources(AppleFileSystemDriver.kext PRIVATE
    AppleFileSystemDriver.cpp
)

target_include_directories(AppleFileSystemDriver.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOStorageFamily/include)
target_link_libraries(AppleFileSystemDriver.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(AppleFileSystemDriver.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual)
set_property(TARGET AppleFileSystemDriver.kext PROPERTY CXX_STANDARD 14)
install(TARGETS AppleFileSystemDriver.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
