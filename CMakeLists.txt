cmake_minimum_required(VERSION 3.8)
project(GWSockets)
include_directories( ./include )

set(CMAKE_CXX_STANDARD 11)

# Unix compile flags
SET(UNIX_COMPILE_FLAGS "-m32 -fPIC")


if (APPLE)
    link_directories( ./libs/macosx )
    option(DOPENSSL_ROOT_DIR "Open SSL directory" /opt/local)
    SET( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${UNIX_COMPILE_FLAGS}" )
    set (CMAKE_SHARED_LINKER_FLAGS "-fPIC, -static-libstdc++")
endif (APPLE)


set(SOURCE_FILES
        src/BlockingQueue.h
        src/glua.cpp
        src/SSLWebSocket.cpp
        src/SSLWebSocket.h
        src/WebSocket.cpp
        src/WebSocket.h
        src/GWSocket.cpp
        src/GWSocket.h)

add_executable(GWSockets ${SOURCE_FILES})

if (APPLE)
    target_link_libraries(GWSockets libcrypto.a)
    target_link_libraries(GWSockets libssl.a)
    target_link_libraries(GWSockets libboost_system.a)
endif (APPLE)
