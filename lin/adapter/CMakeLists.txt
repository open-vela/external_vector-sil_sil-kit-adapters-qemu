add_library(
    ChardevSocketToLinAdapter STATIC
    ChardevSocketToLinAdapter.cpp)

target_include_directories(ChardevSocketToLinAdapter
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_SOURCE_DIR})
target_link_libraries(ChardevSocketToLinAdapter
    PRIVATE
        asio
        SilKit::SilKit
)
set_target_properties(ChardevSocketToLinAdapter
    PROPERTIES
    #ensure SilKit shared libraries can be loaded
    INSTALL_RPATH "$ORIGIN/../lib:$ORIGIN"
    BUILD_RPATH "$ORIGIN"
    RUNTIME_OUTPUT_DIRECTORY $<$<BOOL:ALWAYS>:${SILKIT_ADAPTER_QEMU_OUTPUT_DIRECTORY}>
)

if(MINGW)
    target_compile_definitions(ChardevSocketToLinAdapter INTERFACE _WIN32_WINNT=0x0601)
    target_link_libraries(ChardevSocketToLinAdapter PUBLIC -lwsock32 -lws2_32) #windows socket/ wsa
endif()
