# IOUSBCompositeDriver: real, unmodified Apple source. Splits a multi-
# interface USB device's config into per-interface IOKit matching (each
# published IOUSBInterface gets its own class driver attached).
add_kext_bundle(IOUSBCompositeDriver.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.openosx.driver.IOUSBCompositeDriver BUNDLE_VERSION 1.0.0)
set_property(TARGET IOUSBCompositeDriver.kext PROPERTY OUTPUT_NAME IOUSBCompositeDriver)

target_sources(IOUSBCompositeDriver.kext PRIVATE
    IOUSBCompositeDriver.cpp
)

target_include_directories(IOUSBCompositeDriver.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(IOUSBCompositeDriver.kext PRIVATE IOUSBFamilyHeaders)
target_compile_options(IOUSBCompositeDriver.kext PRIVATE
    -Wno-implicit-int-conversion -Wno-inconsistent-missing-override
    -Wno-sign-conversion -Wno-shadow -Wno-unused-parameter)
set_property(TARGET IOUSBCompositeDriver.kext PROPERTY CXX_STANDARD 11)
install(TARGETS IOUSBCompositeDriver.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
