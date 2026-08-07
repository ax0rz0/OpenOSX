add_kext_bundle(IOATABlockStorage.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.apple.iokit.IOATABlockStorage BUNDLE_VERSION 1.3.0)
set_property(TARGET IOATABlockStorage.kext PROPERTY OUTPUT_NAME IOATABlockStorage)

target_sources(IOATABlockStorage.kext PRIVATE
    IOATABlockStorageDriver.cpp
    IOATABlockStorageCommands.cpp
    IOATABlockStorageDevice.cpp
    IOATABlockStorageDriverPM.cpp
)

target_include_directories(IOATABlockStorage.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOStorageFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOATAFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOATAFamily/private_include)
target_link_libraries(IOATABlockStorage.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(IOATABlockStorage.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-deprecated-declarations
    -Wno-unused-variable -Wno-unused-function -Wno-cast-qual)
set_property(TARGET IOATABlockStorage.kext PROPERTY CXX_STANDARD 14)
install(TARGETS IOATABlockStorage.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
