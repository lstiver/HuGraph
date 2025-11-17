# HuGraph - 高性能存算分离式存储查询系统
## 项目概述
HuGraph 是一个基于 C++17 的云原生RDF存储查询系统，集成了 LevelDB、Apache Arrow 和阿里云 OSS SDK，能够在满足时间约束的情况下最小化经济成本，同时提供可接受的查询性能。

### 目录结构
text
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
### 实验环境
操作系统: Linux iZn4a7auawqabklo8wd4xuZ 5.10.134-19.1.al8.x86_64

编译器: GCC 10.2.1 20200825

构建工具: CMake 3.26.5

硬件配置: 2CPU 8 GB RAM

### 依赖安装
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
#### Ubuntu/Debian
sudo apt-get install -y libleveldb-dev

#### CentOS/RHEL
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

方法一：包管理器安装
参考apacha arrow[官方安装指南](https://arrow.apache.org/install/)

方法二：源码编译安装
参考apacha arrow[官方构建文档](https://arrow.apache.org/docs/developers/cpp/building.html#building-arrow-cpp)

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
构建指南
bash
# 克隆项目
>git clone https://github.com/lstiver/HuGraph.git
>cd HuGraph

# 构建项目
mkdir build && cd build
cmake ..
make

# 运行程序
./S3C++
功能特性
🚀 高性能: 基于 C++17 和现代编译优化

💾 存算分离: 集成阿里云 OSS 实现存储与计算分离

📊 多格式支持: 通过 Apache Arrow 支持多种数据格式

🔍 高效查询: 优化的查询算法和数据处理管道

# 贡献
欢迎提交 Issue 和 Pull Request 来改进项目。

# 联系我们
如有问题，请通过邮箱: hnu16pp@hnu.edu.cn 联系我们