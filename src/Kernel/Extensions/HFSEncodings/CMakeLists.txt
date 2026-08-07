add_kext_bundle(HFSEncodings.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.apple.filesystems.hfs.encodings.kext BUNDLE_VERSION 1.0)
set_property(TARGET HFSEncodings.kext PROPERTY OUTPUT_NAME HFSEncodings)

target_sources(HFSEncodings.kext PRIVATE
    hfs_encodings.c
    hfs_encodinghint.c
    hfs_encodings_kext.cpp
)

target_include_directories(HFSEncodings.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../hfs/core)
target_link_libraries(HFSEncodings.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(HFSEncodings.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual)
set_property(TARGET HFSEncodings.kext PROPERTY CXX_STANDARD 14)
install(TARGETS HFSEncodings.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
