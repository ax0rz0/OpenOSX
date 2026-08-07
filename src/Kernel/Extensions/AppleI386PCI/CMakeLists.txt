add_kext_bundle(AppleI386PCI INFO_PLIST Info.plist BUNDLE_IDENTIFIER com.apple.driver.AppleI386PCI BUNDLE_VERSION 1.4)
target_sources(AppleI386PCI PRIVATE
    AppleI386AGP.cpp
    AppleI386PCI.cpp
)
target_compile_options(AppleI386PCI PRIVATE -Wno-implicit-int-conversion -Wno-sign-conversion)

target_link_libraries(AppleI386PCI PRIVATE IOPCIFamilyHeaders)

install(TARGETS AppleI386PCI DESTINATION System/Library/Extensions COMPONENT BaseSystem)
