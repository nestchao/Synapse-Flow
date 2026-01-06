cmake_minimum_required(VERSION 3.24)

# 🚀 1. VCPKG INTEGRATION (MUST BE BEFORE 'project')
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
else()
    find_program(VCPKG_EXEC vcpkg)
    if(VCPKG_EXEC)
        get_filename_component(VCPKG_ROOT_DIR "${VCPKG_EXEC}" DIRECTORY)
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
        message(STATUS "🚀 Found VCPKG at: ${VCPKG_ROOT_DIR}")
    else()
        # Common Fallbacks
        if(EXISTS "D:/vcpkg/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
        elseif(EXISTS "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
        endif()
    endif()
endif()

project(synapse_flow_backend VERSION 2.1.0 LANGUAGES C CXX)

# 🚀 SETTINGS
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 🚀 FIND PACKAGES (Removed explicit absl/utf8_range to rely on gRPC deps)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(OpenMP REQUIRED)
find_package(faiss CONFIG REQUIRED)

if(NOT faiss_FOUND)
    find_path(FAISS_INCLUDE_DIRS faiss/Index.h)
    find_library(FAISS_LIBRARIES NAMES faiss)
endif()

find_package(httplib CONFIG)
if(NOT httplib_FOUND)
    find_path(HTTPLIB_INCLUDE_DIR httplib.h)
    if(HTTPLIB_INCLUDE_DIR)
        add_library(httplib::httplib INTERFACE IMPORTED)
        target_include_directories(httplib::httplib INTERFACE ${HTTPLIB_INCLUDE_DIR})
        if(WIN32) 
            target_link_libraries(httplib::httplib INTERFACE ws2_32 crypt32 cryptui)
        endif()
    endif()
endif()

# Robust Tree-sitter
find_path(TREESITTER_INCLUDE_DIR tree_sitter/api.h)
find_library(TREESITTER_LIBRARY NAMES tree-sitter libtree-sitter tree-sitter.lib)

# 🚀 ELITE GRAMMAR LIBRARY
add_library(grammars STATIC
    third_party/grammars/parser_cpp.c
    third_party/grammars/scanner_cpp.c
    third_party/grammars/parser_python.c
    third_party/grammars/scanner_python.c
    # third_party/grammars/parser_typescript.c
    # third_party/grammars/scanner_typescript.c
)

target_include_directories(grammars PRIVATE 
    ${TREESITTER_INCLUDE_DIR} 
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/grammars"
)

# 🚀 PROTOBUF GENERATION
get_target_property(grpc_cpp_plugin gRPC::grpc_cpp_plugin LOCATION)
set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto") 
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(PROTO_FILES "${PROTO_SRC_DIR}/agent.proto")
set(PROTO_SRCS "${PROTO_GEN_DIR}/agent.pb.cc" "${PROTO_GEN_DIR}/agent.grpc.pb.cc")
set(PROTO_HDRS "${PROTO_GEN_DIR}/agent.pb.h" "${PROTO_GEN_DIR}/agent.grpc.pb.h")

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND protobuf::protoc
    ARGS --grpc_out=${PROTO_GEN_DIR} --cpp_out=${PROTO_GEN_DIR} --plugin=protoc-gen-grpc=${grpc_cpp_plugin} -I ${PROTO_SRC_DIR} ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
)

# 🚀 SOURCE GROUPING
set(CORE_SOURCES
    src/embedding_service.cpp
    src/retrieval_engine.cpp
    src/faiss_vector_store.cpp
    src/code_graph.cpp
    src/cache_manager.cpp
    src/sync_service.cpp
    src/parser_elite.cpp       
    src/tools/FileSystemTools.cpp
    src/tools/WebSearchTool.cpp
    src/tools/VisionTool.cpp
)

# 🚀 TARGET 1: REST API SERVER
add_executable(code_assistance_server
    src/main.cpp
    src/agent/AgentService.cpp
    src/agent/AgentExecutor.cpp
    src/agent/SubAgent.cpp
    ${CORE_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(code_assistance_server PRIVATE include ${PROTO_GEN_DIR} ${TREESITTER_INCLUDE_DIR})

target_link_libraries(code_assistance_server PRIVATE
    nlohmann_json::nlohmann_json 
    spdlog::spdlog 
    cpr::cpr 
    faiss 
    OpenMP::OpenMP_CXX 
    httplib::httplib 
    ${TREESITTER_LIBRARY}
    grammars 
    protobuf::libprotobuf 
    gRPC::grpc++ 
)

# 🚀 TARGET 2: gRPC AGENT SERVICE
add_executable(agent_service 
    src/agent_main.cpp
    src/agent/AgentExecutor.cpp
    src/agent/SubAgent.cpp
    src/agent/AgentService.cpp 
    ${CORE_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(agent_service PRIVATE include ${PROTO_GEN_DIR} ${TREESITTER_INCLUDE_DIR})

target_link_libraries(agent_service PRIVATE
    gRPC::grpc++ 
    protobuf::libprotobuf 
    nlohmann_json::nlohmann_json 
    spdlog::spdlog 
    cpr::cpr 
    faiss 
    OpenMP::OpenMP_CXX 
    ${TREESITTER_LIBRARY}
    grammars 
)

if(WIN32)
    target_link_libraries(code_assistance_server PRIVATE pdh.lib psapi.lib)
    target_link_libraries(agent_service PRIVATE pdh.lib psapi.lib)
endif()

# ASSETS
add_custom_command(TARGET code_assistance_server POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/keys.json" "$<TARGET_FILE_DIR:code_assistance_server>/keys.json")
add_custom_command(TARGET code_assistance_server POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/www" "$<TARGET_FILE_DIR:code_assistance_server>/www")