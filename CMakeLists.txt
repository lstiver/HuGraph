cmake_minimum_required(VERSION 3.10)
project(S3C++)

#-----------------------------
# 基础配置
#-----------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

# 打印编译信息
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

#-----------------------------
# 依赖查找
#-----------------------------
# LevelDB (通过 add_subdirectory 引入)
# add_subdirectory(leveldb)
find_package(leveldb REQUIRED)

# Arrow 相关
find_package(Arrow REQUIRED)
find_package(ArrowAcero REQUIRED)
find_package(ArrowDataset REQUIRED)
# find_package(Arrow_CSV REQUIRED)

# Arrow CSV (通过 pkg-config)
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(ARROW_CSV REQUIRED arrow-csv)

# 阿里云 OSS SDK 静态库
set(ALIBABACLOUD_OSS_STATIC_LIB "/usr/local/lib64/libalibabacloud-oss-cpp-sdk.a")
set(ALIBABACLOUD_OSS_INCLUDE_DIR "/usr/local/include")

# 检查静态库是否存在
if(NOT EXISTS ${ALIBABACLOUD_OSS_STATIC_LIB})
    message(FATAL_ERROR "阿里云 OSS SDK 静态库未找到: ${ALIBABACLOUD_OSS_STATIC_LIB}")
endif()

#-----------------------------
# 包含目录
#-----------------------------
include_directories(/usr/local/include)
include_directories(/usr/include/)
include_directories(
    ${PROJECT_SOURCE_DIR}/translate
    ${PROJECT_SOURCE_DIR}/sort
    ${PROJECT_SOURCE_DIR}/queryMethod
    ${PROJECT_SOURCE_DIR}/algorithm
    ${PROJECT_SOURCE_DIR}/leveldb
    ${PROJECT_SOURCE_DIR}/ArrowInputStream
    ${ALIBABACLOUD_OSS_INCLUDE_DIR}
)

#-----------------------------
# 可执行文件配置
#-----------------------------
add_executable(${PROJECT_NAME}
    main.cpp
    translate/translate.cpp
    sort/sort.cpp
    queryMethod/query.cpp
    algorithm/merge.cpp
    ArrowInputStream/ArrowInputStream.cpp
)

#-----------------------------
# 链接库配置
#-----------------------------
target_link_libraries(${PROJECT_NAME} PUBLIC
    # 基础库
    leveldb
    pthread
    m
    dl
    curl
    ssl
    crypto

    # Arrow 相关
    Arrow::arrow_shared
    ArrowAcero::arrow_acero_shared
    ArrowDataset::arrow_dataset_shared
    ${ARROW_CSV_LIBRARIES}
    # 阿里云 OSS SDK 静态库
    ${ALIBABACLOUD_OSS_STATIC_LIB}
)