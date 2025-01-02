add_executable(sil-kit-demo-lin-echo-device
    "SilKitDemoLinEchoDevice.cpp"
    "${CMAKE_SOURCE_DIR}/adapter/Parsing.cpp"
    "${CMAKE_SOURCE_DIR}/adapter/SignalHandler.cpp"
)
target_include_directories(sil-kit-demo-lin-echo-device
    PRIVATE
    "${CMAKE_SOURCE_DIR}"
)
target_link_libraries(sil-kit-demo-lin-echo-device
    PRIVATE
    asio
    SilKit::SilKit
)
set_target_properties(sil-kit-demo-lin-echo-device
    PROPERTIES
    #ensure SilKit shared libraries can be loaded
    INSTALL_RPATH "$ORIGIN/../lib:$ORIGIN"
    BUILD_RPATH "$ORIGIN"
    RUNTIME_OUTPUT_DIRECTORY $<$<BOOL:ALWAYS>:${SILKIT_ADAPTER_QEMU_OUTPUT_DIRECTORY}>
)
