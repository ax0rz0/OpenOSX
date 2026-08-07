add_kext_bundle(apfs.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.apple.filesystems.apfs BUNDLE_VERSION 0.1
)
set_property(TARGET apfs.kext PROPERTY OUTPUT_NAME apfs)

target_sources(apfs.kext PRIVATE
    apfs_btree.c
    apfs_iokit.cpp
    apfs_vfsops.c
    apfs_vnops.c
)

target_link_libraries(apfs.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(apfs.kext PRIVATE
    -DKERNEL=1 -DKERNEL_PRIVATE=1
    -Wno-sign-conversion -Wno-implicit-int-conversion)
install(TARGETS apfs.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
