add_kext_bundle(IONVMEFamily.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.apple.iokit.IONVMEFamily BUNDLE_VERSION 1.0.0)
set_property(TARGET IONVMEFamily.kext PROPERTY OUTPUT_NAME IONVMEFamily)

target_sources(IONVMEFamily.kext PRIVATE
    IONVMEController.cpp
    IONVMEDisk.cpp
)

target_include_directories(IONVMEFamily.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOPCIFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOStorageFamily/include)
target_link_libraries(IONVMEFamily.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(IONVMEFamily.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual)
set_property(TARGET IONVMEFamily.kext PROPERTY CXX_STANDARD 14)
install(TARGETS IONVMEFamily.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
