# HuGraph：An Economical Cloud-Native RDF Store
## Overview
HuGraph 是一个基于 C++17 的云原生RDF存储查询系统，集成了 LevelDB、Apache Arrow 和阿里云 OSS SDK，能够在满足时间约束的情况下最小化经济成本，同时提供可接受的查询性能。目前有两个版本，分别基于amazon和aliyun，amazon在main分支下，aliyun在分支aliyun下。

功能特性

🚀 高性能: 基于 C++17 和现代编译优化

💾 存算分离: 集成阿里云 OSS 实现存储与计算分离

📊 多格式支持: 通过 Apache Arrow 支持多种数据格式

🔍 高效查询: 优化的查询算法和数据处理管道

### Experimental environment
操作系统: Linux iZn4a7auawqabklo8wd4xuZ 5.10.134-19.1.al8.x86_64

编译器: GCC 10.2.1 20200825

构建工具: CMake 3.26.5

硬件配置: 2CPU 8 GB RAM

## Quickstart
### Install dependencies
1. 基础开发工具

```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y \
    cmake3 \
    pkgconfig \
    libcurl-devel \
    openssl-devel \
    zlib-devel
```
2. LevelDB 安装

方法一：包管理器安装
```bash
sudo yum install -y leveldb-devel
```

方法二：源码编译安装

```bash
git clone https://github.com/google/leveldb.git
cd leveldb
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON ..
make -j$(nproc)
sudo make install
```

3. Apache Arrow 安装

需要使用到的模块有：arrow_shared,ArrowAcero,ArrowDataset,ArrowCompute

方法一：包管理器安装
参考apacha arrow[官方安装指南](https://arrow.apache.org/install/)

方法二：源码编译安装
参考apacha arrow[官方构建文档](https://arrow.apache.org/docs/developers/cpp/building.html#building-arrow-cpp)

4. 阿里云 OSS SDK 安装
```bash
git clone https://github.com/aliyun/aliyun-oss-cpp-sdk.git
cd aliyun-oss-cpp-sdk
mkdir build && cd build

cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    ..

make -j$(nproc)
sudo make install
```

5. Amazon AWS SDK安装（如果使用阿里云部署，无需安装）

```bash
sudo yum install libcurl-devel openssl-devel libuuid-devel pulseaudio-libs-devel
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp
mkdir sdk_build
cd sdk_build
cmake ../aws-sdk-cpp -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/usr/local/ -DCMAKE_INSTALL_PREFIX=/usr/local/ -DBUILD_ONLY="s3"
cmake --build . --config=Debug
cmake --install . --config=Debug
```

### Build Project
1. 克隆项目
```bash
git clone https://github.com/lstiver/HuGraph.git
cd HuGraph
```
2. 配置环境变量

#### Amazon 
```bash
aws configure
```

#### aliyun

edpoint默认为华中-武汉，可自行在main函数中修改
```bash
echo 'export ALIBABA_CLOUD_ACCESS_KEY_ID="LTAI5tYourAccessKeyId"' >> ~/.bashrc
echo 'export ALIBABA_CLOUD_ACCESS_KEY_SECRET="K4HcYourAccessKeySecret"' >> ~/.bashrc
source ~/.bashrc
```

2. 编译项目
```bash
mkdir build && cd build
cmake ..
make
```

3. 运行程序
```bash
./S3C++
```

# 贡献
欢迎提交 Issue 和 Pull Request 来改进项目。

# 联系我们
如有问题，请通过邮箱: hnu16pp@hnu.edu.cn 联系我们