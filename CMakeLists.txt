cmake_minimum_required(VERSION 3.10)
project(gwsockets)
add_subdirectory(GmodLUA)

file(GLOB_RECURSE GWSOCKETS_SRC "src/*.h" "src/*.cpp")
set(SOURCE_FILES ${GWSOCKETS_SRC})
set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_CXX_STANDARD 14)

add_library(gwsockets SHARED ${SOURCE_FILES})
target_link_libraries(gwsockets gmod-module-base)

target_include_directories(gwsockets PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

if(WIN32)
    target_compile_definitions(gwsockets PRIVATE WIN32 GMMODULE BOOST_ALL_NO_LIB NDEBUG)
else()
    target_compile_definitions(gwsockets PRIVATE LINUX GMMODULE BOOST_ALL_NO_LIB NDEBUG)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(WIN32)
        find_library(CRYPTO_LIB libcrypto HINTS "${PROJECT_SOURCE_DIR}/lib64/windows")
        find_library(SSL_LIB libssl HINTS "${PROJECT_SOURCE_DIR}/lib64/windows")
        find_library(BOOST_SYSTEM_LIB boost_system HINTS "${PROJECT_SOURCE_DIR}/lib64/windows")
    else()
        find_library(CRYPTO_LIB crypto HINTS "${PROJECT_SOURCE_DIR}/lib64/linux")
        find_library(SSL_LIB ssl HINTS "${PROJECT_SOURCE_DIR}/lib64/linux")
        find_library(BOOST_SYSTEM_LIB boost_system HINTS "${PROJECT_SOURCE_DIR}/lib64/linux")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    if(WIN32)
        find_library(CRYPTO_LIB libcrypto HINTS "${PROJECT_SOURCE_DIR}/lib/windows")
        find_library(SSL_LIB libssl HINTS "${PROJECT_SOURCE_DIR}/lib/windows")
        find_library(BOOST_SYSTEM_LIB boost_system HINTS "${PROJECT_SOURCE_DIR}/lib/windows")
    else()
        find_library(CRYPTO_LIB crypto HINTS "${PROJECT_SOURCE_DIR}/lib/linux")
        find_library(SSL_LIB ssl HINTS "${PROJECT_SOURCE_DIR}/lib/linux")
        find_library(BOOST_SYSTEM_LIB boost_system HINTS "${PROJECT_SOURCE_DIR}/lib/linux")
    endif()
endif()

if(WIN32)
    target_link_libraries(gwsockets ${SSL_LIB} ${CRYPTO_LIB} ${BOOST_SYSTEM_LIB} crypt32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(gwsockets ${SSL_LIB} ${CRYPTO_LIB} ${BOOST_SYSTEM_LIB} Threads::Threads ${CMAKE_DL_LIBS})
    target_link_libraries(gwsockets -static-libstdc++)
endif()


set_gmod_suffix_prefix(gwsockets)
