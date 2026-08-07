add_kext_bundle(AppleIntelPIIXATA KERNEL_PRIVATE INFO_PLIST Info.plist BUNDLE_IDENTIFIER com.apple.driver.AppleIntelPIIXATA BUNDLE_VERSION 2.5.1)
target_sources(AppleIntelPIIXATA PRIVATE
    AppleIntelICHxSATA.cpp
    AppleIntelPIIXATAChannel.cpp
    AppleIntelPIIXATARoot.cpp
    AppleIntelPIIXPATA.cpp
)

# QEMU's IDE (like VPC4) leaves the ERR status bit set after a soft reset (SRST),
# which makes IOATAController::scanForDrives() reject the drive.  Enable the
# workaround that re-scans unknown units ignoring the ERR bit so the disk is
# actually detected and a device nub gets published.
target_compile_options(AppleIntelPIIXATA PRIVATE -DENABLE_VPC4_DRIVESCAN_WORKAROUND=1 -Wno-implicit-int-conversion -Wno-shorten-64-to-32 -Wno-sign-conversion)
target_link_libraries(AppleIntelPIIXATA PRIVATE IOACPIFamilyHeaders IOATAFamilyHeaders IOATAFamilyPrivateHeaders IOPCIFamilyHeaders IOStorageFamilyHeaders)
install(TARGETS AppleIntelPIIXATA DESTINATION System/Library/Extensions/IOATAFamily.kext/Contents/PlugIns COMPONENT BaseSystem)
add_dependencies(AppleIntelPIIXATA IOATAFamily)
