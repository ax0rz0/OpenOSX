if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()

file(WRITE "${OUTPUT}" "--- !tapi-tbd
tbd-version:     4
targets:         [ x86_64-macos ]
install-name:    /usr/lib/system/libsystem_pthread.dylib
current-version: 454.100.8
exports:
  - targets:         [ x86_64-macos ]
    symbols:         [ __pthread_is_threaded, _pthread_self ]
...
")
