add_kext_bundle(msdosfs.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.apple.filesystems.msdosfs BUNDLE_VERSION 1.0
)
set_property(TARGET msdosfs.kext PROPERTY OUTPUT_NAME msdosfs)

target_sources(msdosfs.kext PRIVATE
    msdosfs_conv.c
    msdosfs_denode.c
    msdosfs_fat.c
    msdosfs_iokit.cpp
    msdosfs_lookup.c
    msdosfs_vfsops.c
    msdosfs_vnops.c
)

target_link_libraries(msdosfs.kext PRIVATE xnu_kernel_private_headers)
target_compile_definitions(msdosfs.kext PRIVATE
    DEBUG=0
    KERNEL=1
    MSDOSFS_AUTO_UNLOAD=0
    TARGET_OS_OSX=1
)
target_compile_options(msdosfs.kext PRIVATE
    -Wno-deprecated-declarations
    -Wno-implicit-int-conversion
    -Wno-sign-conversion
)
install(TARGETS msdosfs.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
