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
# LevelDB (直接链接已编译的库)
# add_subdirectory(leveldb)  # 注释掉以避免权限问题
set(LEVELDB_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/leveldb/include)
set(LEVELDB_LIBRARY ${PROJECT_SOURCE_DIR}/leveldb/libleveldb.a)

# Arrow 相关
find_package(Arrow REQUIRED)
find_package(ArrowAcero REQUIRED)
find_package(ArrowDataset REQUIRED)

# Arrow CSV (通过 pkg-config)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ARROW_CSV REQUIRED arrow-csv)

# AWS SDK (可选，根据需要取消注释)
# find_package(AWSSDK REQUIRED COMPONENTS s3 transfer)

#-----------------------------
# 包含目录
#-----------------------------
include_directories(
    ${PROJECT_SOURCE_DIR}/translate
    ${PROJECT_SOURCE_DIR}/sort
    ${PROJECT_SOURCE_DIR}/queryMethod
    ${PROJECT_SOURCE_DIR}/algorithm
    ${PROJECT_SOURCE_DIR}/leveldb
    ${PROJECT_SOURCE_DIR}/ArrowInputStream
    ${PROJECT_SOURCE_DIR}/update
    ${Python3_INCLUDE_DIRS}
    /usr/local/include/aws
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
    update/update.cpp
)

#-----------------------------
# 链接库配置
#-----------------------------
target_link_libraries(${PROJECT_NAME} PUBLIC
    # 基础库
    ${LEVELDB_LIBRARY}
    pthread
    m
    dl
    snappy

    # Arrow 相关
    Arrow::arrow_shared
    ArrowAcero::arrow_acero_shared
    ArrowDataset::arrow_dataset_shared
    ${ARROW_CSV_LIBRARIES}

    # AWS SDK 
    aws-cpp-sdk-core
    aws-cpp-sdk-s3
    aws-cpp-sdk-transfer
)