# HuGraph: An Economical Cloud-Native RDF Store

## Overview

HuGraph is a cloud-native RDF storage and query system built on C++17, integrating LevelDB, Apache Arrow, and Alibaba Cloud OSS SDK. It aims to minimize economic costs while meeting time constraints and providing acceptable query performance. Currently, two versions are available: the Amazon-based version is on the `main` branch, and the Aliyun-based version is on the `aliyun` branch.

## Features

🚀 **High Performance**: Built on C++17 with modern compilation optimizations

💾 **Storage-Computation Separation**: Integrated with Alibaba Cloud OSS to decouple storage and computation

📊 **Multi-format Support**: Supports various data formats through Apache Arrow

🔍 **Efficient Querying**: Optimized query algorithms and data processing pipelines

## Experimental Environment

- **Operating System**: Linux iZn4a7auawqabklo8wd4xuZ 5.10.134-19.1.al8.x86_64
- **Compiler**: GCC 10.2.1 20200825
- **Build Tool**: CMake 3.26.5
- **Hardware Configuration**: 2 CPUs, 8 GB RAM

## Quickstart

### Install Dependencies

#### 1. Basic Development Tools
```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y \
    cmake3 \
    pkgconfig \
    libcurl-devel \
    openssl-devel \
    zlib-devel
```

#### 2. LevelDB Installation

**Option 1: Package Manager Installation**
```bash
sudo yum install -y leveldb-devel
```

**Option 2: Source Code Compilation**
```bash
git clone https://github.com/google/leveldb.git
cd leveldb
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON ..
make -j$(nproc)
sudo make install
```

#### 3. Apache Arrow Installation

**Required modules**: `arrow_shared`, `ArrowAcero`, `ArrowDataset`, `ArrowCompute`

**Option 1: Package Manager Installation**  
Refer to the [Apache Arrow Official Installation Guide](https://arrow.apache.org/install/)

**Option 2: Source Code Compilation**  
Refer to the [Apache Arrow Official Build Documentation](https://arrow.apache.org/docs/developers/cpp/building.html#building-arrow-cpp)

#### 4. Alibaba Cloud OSS SDK Installation
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

#### 5. Amazon AWS SDK Installation (Optional, only needed for Amazon deployment)
```bash
sudo yum install libcurl-devel openssl-devel libuuid-devel pulseaudio-libs-devel
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp
mkdir sdk_build
cd sdk_build
cmake ../aws-sdk-cpp -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/usr/local/ -DCMAKE_INSTALL_PREFIX=/usr/local/ -DBUILD_ONLY="s3"
cmake --build . --config=Debug
cmake --install . --config=Debug
```

### Build the Project

#### 1. Clone the Repository
```bash
git clone https://github.com/lstiver/HuGraph.git
cd HuGraph
```

#### 2. Configure Environment Variables

**For Amazon Deployment:**
```bash
aws configure
```

**For Aliyun Deployment:**
```bash
echo 'export ALIBABA_CLOUD_ACCESS_KEY_ID="LTAI5tYourAccessKeyId"' >> ~/.bashrc
echo 'export ALIBABA_CLOUD_ACCESS_KEY_SECRET="K4HcYourAccessKeySecret"' >> ~/.bashrc
source ~/.bashrc
```

*Note: The endpoint defaults to Central China (Wuhan). You can modify it in the `main` function if needed.*

#### 3. Build the Project
```bash
mkdir build && cd build
cmake ..
make
```

#### 4. Run the Program
```bash
./S3C++
```

## Contributing

We welcome Issue submissions and Pull Requests to improve the project.

## Contact Us

For any questions, please contact us via email: hnu16pp@hnu.edu.cn
