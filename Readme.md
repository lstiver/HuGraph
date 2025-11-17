# 项目概述
HuGraph 是一个基于 C++17 的高性能存算分离式存储查询系统，集成了 LevelDB、Apache Arrow 和阿里云 OSS SDK，提供高效的数据处理和查询能力。

目录结构
S3C++/
├── CMakeLists.txt              # 项目构建配置文件
├── main.cpp                   # 主程序入口
├── translate/                 # 数据转换模块
│   └── translate.cpp
├── sort/                      # 排序算法模块
│   └── sort.cpp
├── queryMethod/               # 查询方法模块
│   └── query.cpp
├── algorithm/                 # 核心算法模块
│   └── merge.cpp
└── ArrowInputStream/          # Arrow 输入流处理模块
    └── ArrowInputStream.cpp
实验环境
操作系统: Linux iZn4a7auawqabklo8wd4xuZ 5.10.134-19.1.al8.x86_64 #1 SMP Wed Jun 25 10:21:27 CST 2025 x86_64 x86_64 x86_64 GNU/Linux

编译器: gcc (GCC) 10.2.1 20200825 (Alibaba 10.2.1-3.8 2.32)

构建工具: cmake version 3.26.5

cpu与内存: 2CPU 8 GB RAM

依赖安装
1. 基础开发工具
Ubuntu/Debian:

bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libcurl4-openssl-dev \
    libssl-dev \
    zlib1g-dev
CentOS/RHEL:

bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y \
    cmake3 \
    pkgconfig \
    libcurl-devel \
    openssl-devel \
    zlib-devel
    
2. LevelDB 安装
方法一：包管理器安装

bash
# Ubuntu/Debian
sudo apt-get install -y libleveldb-dev

# CentOS/RHEL
sudo yum install -y leveldb-devel
方法二：源码编译安装

bash
git clone https://github.com/google/leveldb.git
cd leveldb
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON ..
make -j$(nproc)
sudo make install
3. Apache Arrow 安装
需要使用到的模块有：
arrow_shared
ArrowAcero
ArrowDataset
ArrowCompute
方法一：包管理器安装，参考网址：https://arrow.apache.org/install/
方法二：源码编译安装：参考网址：https://arrow.apache.org/docs/developers/cpp/building.html#building-arrow-cpp
4. 阿里云 OSS SDK 安装
bash
git clone https://github.com/aliyun/aliyun-oss-cpp-sdk.git
cd aliyun-oss-cpp-sdk
mkdir build && cd build

cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    ..

make -j$(nproc)
sudo make install

# 构建指南
git clone https://github.com/lstiver/HuGraph.git
进入目录文件
mkdir build & cd build
cmake ..
make
运行可执行文件
./S3C++