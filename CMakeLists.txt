#set the minimum required version of cmake
cmake_minimum_required(VERSION 3.10)

project(LoreBook)
#set the C++ standard to use
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


file(GLOB_RECURSE CLIENT_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
# exclude LineageEditor.cpp
list(FILTER CLIENT_FILES EXCLUDE REGEX ".*/LineageEditor\\.cpp$")
list(FILTER CLIENT_FILES EXCLUDE REGEX ".*/BuildingEditor\\.cpp$")
list(FILTER CLIENT_FILES EXCLUDE REGEX ".*/TimeEditor\\.cpp$")
list(FILTER CLIENT_FILES EXCLUDE REGEX ".*/VariableEditor\\.cpp$")
add_executable(${PROJECT_NAME} ${CLIENT_FILES})
add_subdirectory(LoreBook_Resources)
target_link_libraries(${PROJECT_NAME} PRIVATE LoreBook_Resources)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(OpenGL_GL_PREFERENCE "GLVND")

#opengl
find_package(OpenGL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL OpenGL::GLU)

#glew
find_package(GLEW REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE GLEW::GLEW)

#glfw
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)

#glm
find_package(glm CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)

#imgui
find_package(imgui CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui)

#freetype
find_package(Freetype REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Freetype::Freetype)

#nlohmann_json
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

#stb
find_package(Stb REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIR})

# md4c (CommonMark parser)
find_package(md4c CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE md4c::md4c)

#openssl
find_package(OpenSSL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::Crypto)

# Crypto++
find_package(cryptopp CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE cryptopp::cryptopp)

#curl
find_package(CURL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

#lua
find_package(Lua REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LUA_LIBRARIES})

#boost
find_package(boost_dll CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::dll)

#plog
find_package(plog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE plog::plog)

# Boost Beast (for HTTP/HTTPS support)
find_package(Boost CONFIG REQUIRED COMPONENTS beast)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::beast)

#cli11
find_package(CLI11 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

#ffmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswresample
)
target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})

#glut
find_package(X11 REQUIRED)
find_package(GLUT REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE GLUT::GLUT Xi Xrandr Xxf86vm)

# assimp (3D model import)
find_package(assimp CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE assimp::assimp)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE unofficial::sqlite3::sqlite3)

# MySQL Connector/C++ (required via vcpkg "unofficial" port)
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE unofficial::mysql-connector-cpp::connector)
# The mysql connector X devapi currently links against protobuf and absl components which
# may not be transitively added by the connector imported target on all platforms.
# Add explicit links so the global executable is satisfied.
find_package(protobuf CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
# Link to protobuf and a few absl components that the connector's protobuf code may need
target_link_libraries(${PROJECT_NAME} PRIVATE protobuf::libprotobuf absl::strings absl::raw_logging_internal absl::log_severity)
# Link LZ4 from vcpkg install path when system -llz4 is not available
find_package(lz4 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE lz4::lz4)

# Some resolver functions (ns_initparse/ns_parserr) are provided by libresolv on
# some platforms; link it after MySQL connector so static libraries get the
# resolver library at the right place in the link order.
target_link_libraries(${PROJECT_NAME} PRIVATE resolv)

#END YOUR LIBRARIES

#create a ${BIN_PATH} in source and move the executable there after build
set(BIN_PATH bin)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/${BIN_PATH})
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/${BIN_PATH})
