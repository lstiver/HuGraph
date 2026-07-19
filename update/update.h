#ifndef UPDATE_H
#define UPDATE_H

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/transfer/TransferManager.h>
#include <aws/transfer/TransferHandle.h>
#include <leveldb/db.h>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/csv/api.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <memory>

using namespace std;

// 三元组结构
struct Triple {
    string subject;
    string predicate;
    string object;
};

// 更新操作类型
enum class UpdateType {
    INSERT,
    DELETE
};

// 解析三元组文件
vector<Triple> parseTripleFile(const string& file_path);

// 主更新函数
void executeUpdate(
    const string& bucket,
    const string& triple_file_path,
    UpdateType update_type,
    shared_ptr<Aws::S3::S3Client> awsClient
);

// Insert操作
void insertTriple(
    const string& bucket,
    const Triple& triple,
    const string& value1,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db,
    leveldb::DB* result_db
);

// Delete操作
void deleteTriple(
    const string& bucket,
    const Triple& triple,
    const string& value1,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db
);

// 创建新文件并上传到S3
void createNewFileAndUpload(
    const string& bucket,
    const string& predicate,
    const Triple& triple,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db,
    leveldb::DB* result_db
);

// 追加到现有文件并重新上传
void appendToExistingFileAndUpload(
    const string& bucket,
    const string& predicate,
    const Triple& triple,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db,
    leveldb::DB* result_db
);

// 从S3下载文件到本地
bool downloadFileFromS3(
    const string& bucket,
    const string& key,
    const string& local_path,
    shared_ptr<Aws::S3::S3Client> awsClient
);

// 上传文件到S3
bool uploadFileToS3(
    const string& bucket,
    const string& key,
    const string& local_path,
    shared_ptr<Aws::S3::S3Client> awsClient
);

// 创建索引文件并上传
void createAndUploadIndexFile(
    const string& bucket,
    const string& predicate,
    size_t file_size,
    shared_ptr<Aws::S3::S3Client> awsClient,
    const string& object_id = ""
);

// 更新LevelDB索引
void updateLevelDBIndex(
    leveldb::DB* db,
    const string& predicate,
    const Triple& triple,
    size_t start,
    size_t end
);

// 检查三元组是否已存在
bool tripleExists(
    const string& bucket,
    const string& predicate,
    const Triple& triple,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db
);

#endif // UPDATE_H
