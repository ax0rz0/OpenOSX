cmake_minimum_required(VERSION 3.15)

project(libapfsrw C)

add_library(apfsrw STATIC
    src/apfsrw.c
)

target_include_directories(apfsrw PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(apfsrw PUBLIC c_std_99)
target_compile_options(apfsrw PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_executable(apfsrw-tool
    tools/apfsrw.c
)

set_target_properties(apfsrw-tool PROPERTIES
    OUTPUT_NAME apfsrw
)

target_link_libraries(apfsrw-tool PRIVATE apfsrw)

install(TARGETS apfsrw ARCHIVE DESTINATION lib)
install(TARGETS apfsrw-tool RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
