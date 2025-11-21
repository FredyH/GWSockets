cmake_minimum_required(VERSION 3.10)
project(gwsockets)
add_subdirectory(GmodLUA)

file(GLOB_RECURSE GWSOCKETS_SRC "src/*.h" "src/*.cpp")
set(SOURCE_FILES ${GWSOCKETS_SRC})
set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_CXX_STANDARD 14)

# Find packages from vcpkg
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system chrono)

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

# Link with OpenSSL and Boost from vcpkg
if(WIN32)
    target_link_libraries(gwsockets OpenSSL::SSL OpenSSL::Crypto Boost::system Boost::chrono crypt32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(gwsockets OpenSSL::SSL OpenSSL::Crypto Boost::system Boost::chrono Threads::Threads ${CMAKE_DL_LIBS})
    target_link_libraries(gwsockets -static-libstdc++)
endif()


set_gmod_suffix_prefix(gwsockets)
