#include <iostream>
#include<string>
#include <fstream>
#include <sstream>
#include <leveldb/db.h>
using namespace std;

int main() {
    // 打开 CSV 文件
    std::ifstream file("/data/conversion_index.csv");
    if (!file.is_open()) {
        std::cerr << "无法打开文件!" << std::endl;
        return -1;
    }

    string dbPath = "/home/ec2-user/s3/S3C++/index";

    // 打开levedb
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, dbPath, &db);
    
    if (!status.ok()) {
        std::cerr << "无法打开 LevelDB 数据库: " << status.ToString() << std::endl;
        return -1;
    }

    std::string line;
    // 读取 CSV 文件中的每一行
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string value, key;

        // 将CSV分成两列
        std::getline(ss, key, ',');  // 第一列：int，存储为值
        std::getline(ss, value, ',');        // 第二列：string，存储为键

        // 去除 key 和 value 的前后空白字符
        key.erase(0, key.find_first_not_of(" \t\n\r"));
        key.erase(key.find_last_not_of(" \t\n\r") + 1);
        value.erase(0, value.find_first_not_of(" \t\n\r"));
        value.erase(value.find_last_not_of(" \t\n\r") + 1);

        // 将键值对写入 LevelDB
        status = db->Put(leveldb::WriteOptions(), key, value);

        if (!status.ok()) {
            std::cerr << "无法写入数据到 LevelDB: " << status.ToString() << std::endl;
            file.close();
            delete db;
            exit(1);
        } else {
            std::cout << "写入成功: " << key << " => " << value << std::endl;
        }
    }
    file.close();
    delete db;
    
    std::cout << "写入成功!" << endl;
    return 0;
}
