cmake_minimum_required(VERSION 3.10)
project(Papyrus)
include(FetchContent)
# Fetch nlohmann/json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Fetch SQLiteCpp
FetchContent_Declare(
  SQLiteCpp
  GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
  GIT_TAG 3.3.2
)
FetchContent_MakeAvailable(SQLiteCpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)

# Enable static linking for GCC libraries
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  add_compile_options(-g)
  add_link_options(-g)
endif()

# Add uWebSockets and µSockets
include_directories(/usr/local/include)
link_directories(/usr/local/lib)

# Add your source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE -l:uSockets.a)
target_link_libraries(${PROJECT_NAME} PRIVATE SQLiteCpp sqlite3)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

find_library(SSL_STATIC_LIBRARY libssl.a)
find_library(CRYPTO_STATIC_LIBRARY libcrypto.a)
if(SSL_STATIC_LIBRARY AND CRYPTO_STATIC_LIBRARY)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${SSL_STATIC_LIBRARY} ${CRYPTO_STATIC_LIBRARY})
else()
  message(FATAL_ERROR "Static OpenSSL libraries not found")
endif()

find_library(ZLIB_STATIC_LIBRARY libz.a)
if(ZLIB_STATIC_LIBRARY)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${ZLIB_STATIC_LIBRARY})
else()
  message(FATAL_ERROR "Static Zlib library not found")
endif()

get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "dir='${dir}'")
endforeach()