# AppleUSBMergeNub: real, unmodified Apple source. No personality (see
# Info.plist) - built to confirm it compiles cleanly against our
# IOUSBFamily reconstruction, not currently instantiated by anything.
add_kext_bundle(AppleUSBMergeNub.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.openosx.driver.AppleUSBMergeNub BUNDLE_VERSION 1.0.0)
set_property(TARGET AppleUSBMergeNub.kext PROPERTY OUTPUT_NAME AppleUSBMergeNub)

target_sources(AppleUSBMergeNub.kext PRIVATE
    AppleUSBMergeNub.cpp
)

target_include_directories(AppleUSBMergeNub.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(AppleUSBMergeNub.kext PRIVATE IOUSBFamilyHeaders)
target_compile_options(AppleUSBMergeNub.kext PRIVATE
    -Wno-implicit-int-conversion -Wno-inconsistent-missing-override
    -Wno-sign-conversion -Wno-shadow -Wno-unused-parameter)
set_property(TARGET AppleUSBMergeNub.kext PROPERTY CXX_STANDARD 11)
install(TARGETS AppleUSBMergeNub.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
