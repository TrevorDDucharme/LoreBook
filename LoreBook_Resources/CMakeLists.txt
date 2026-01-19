project(LoreBook_Resources)

# Set highest optimization level with different flags for different compilers
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /Ob2 /DNDEBUG")
endif()

set(GENERATED_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/generated/${CMAKE_PROJECT_NAME}/)
# Create output directory for generated files
file(MAKE_DIRECTORY ${GENERATED_OUTPUT_DIRECTORY})

add_custom_target(${PROJECT_NAME}_source
	COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR}/resources
	COMMAND zip -r - . | ${CMAKE_CURRENT_SOURCE_DIR}/EmbeddingCompiler --outputDirectory=${CMAKE_CURRENT_SOURCE_DIR}/src --fileName=${PROJECT_NAME}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

#static lib for the embedded resources
file(GLOB_RECURSE SHARED_EMBEDDED_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(${PROJECT_NAME} STATIC ${SHARED_EMBEDDED_FILES})
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_source)

#add post build command to copy the header from ./src to ../include/LoreBook_Resources/
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/../include/${PROJECT_NAME}/
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/../include/${PROJECT_NAME}/
)

#zstd
find_package(zstd CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC zstd::libzstd)
find_package(PhysFS CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC $<IF:$<TARGET_EXISTS:PhysFS::PhysFS>,PhysFS::PhysFS,PhysFS::PhysFS-static>)