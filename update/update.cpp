#include "update.h"
#include <aws/core/utils/stream/PreallocatedStreamBuf.h>
#include <aws/core/utils/Array.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <chrono>
#include <ctime>

using namespace Aws::Utils;
using namespace Aws::Transfer;

// 解析三元组文件（支持CSV格式：subject,predicate,object）
vector<Triple> parseTripleFile(const string& file_path) {
    vector<Triple> triples;
    ifstream file(file_path);
    string line;
    
    if (!file.is_open()) {
        spdlog::error("无法打开三元组文件: {}", file_path);
        return triples;
    }
    
    while (getline(file, line)) {
        if (line.empty()) continue;
        
        // 尝试CSV格式解析（逗号分隔）
        vector<string> parts;
        bool in_quotes = false;
        string current_part;
        
        // 手动解析CSV，处理引号内的逗号
        for (size_t i = 0; i < line.length(); i++) {
            char c = line[i];
            
            if (c == '"') {
                in_quotes = !in_quotes;
                current_part += c;
            } else if (c == ',' && !in_quotes) {
                parts.push_back(current_part);
                current_part.clear();
            } else {
                current_part += c;
            }
        }
        parts.push_back(current_part); // 添加最后一个部分
        
        // 如果CSV格式解析成功（至少有3个部分）
        if (parts.size() >= 3) {
            string subject = parts[0];
            string predicate = parts[1];
            string object = parts[2];
            
            // 去除字段周围的引号和空格
            auto trim_quotes = [](string& s) {
                // 去除首尾空格
                size_t start = s.find_first_not_of(" \t\r\n");
                size_t end = s.find_last_not_of(" \t\r\n");
                if (start != string::npos && end != string::npos) {
                    s = s.substr(start, end - start + 1);
                }
                // 去除首尾引号（只去掉一对，保留内容）
                if (s.length() >= 2 && s.front() == '"' && s.back() == '"') {
                    s = s.substr(1, s.length() - 2);
                }
            };
            
            trim_quotes(subject);
            trim_quotes(predicate);
            trim_quotes(object);
            
            // 移除object末尾的点号
            if (!object.empty() && object.back() == '.') {
                object.pop_back();
            }
            
            // 注意：这里不添加尖括号，保持原始格式
            // 如果原始数据有尖括号，它们已经在subject、predicate、object中
            Triple triple;
            triple.subject = subject;
            triple.predicate = predicate;
            triple.object = object;
            triples.push_back(triple);
            
            spdlog::info("解析三元组: {} {} {}", subject, predicate, object);
        } else {
            // 如果CSV格式解析失败，尝试空格分隔格式
            istringstream stream(line);
            string subject, predicate, object;
            if (stream >> subject >> predicate >> object) {
                // 移除object末尾的点号
                if (!object.empty() && object.back() == '.') {
                    object.pop_back();
                }
                
                Triple triple;
                triple.subject = subject;
                triple.predicate = predicate;
                triple.object = object;
                triples.push_back(triple);
                
                spdlog::info("解析三元组: {} {} {}", subject, predicate, object);
            }
        }
    }
    
    file.close();
    return triples;
}

// 批量处理函数声明
void createNewFileAndUploadBatch(
    const string& bucket,
    const string& predicate,
    const vector<Triple>& triples,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db,
    leveldb::DB* result_db
);

void appendToExistingFileAndUploadBatch(
    const string& bucket,
    const string& value1,
    const vector<Triple>& triples,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db,
    leveldb::DB* result_db
);

void deleteTripleBatch(
    const string& bucket,
    const string& predicate,
    const string& value1,
    const vector<Triple>& triples,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db
);

// 主更新函数
void executeUpdate(
    const string& bucket,
    const string& triple_file_path,
    UpdateType update_type,
    shared_ptr<Aws::S3::S3Client> awsClient
) {
    // 打开第一个LevelDB（index）
    string dbPath = "/data/dbpedia1B/index";
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, dbPath, &db);
    
    if (!status.ok()) {
        spdlog::error("无法打开LevelDB: {}", status.ToString());
        return;
    }
    
    // 打开第二个LevelDB（result_index）
    string resultDbPath = "/data/dbpedia1B/result_index";
    leveldb::DB* result_db;
    leveldb::Status result_status = leveldb::DB::Open(options, resultDbPath, &result_db);
    
    if (!result_status.ok()) {
        spdlog::error("无法打开result_index LevelDB: {}", result_status.ToString());
        delete db;
        return;
    }
    
    // 解析三元组文件
    vector<Triple> triples = parseTripleFile(triple_file_path);
    
    if (triples.empty()) {
        spdlog::error("未解析到任何三元组");
        delete db;
        delete result_db;
        return;
    }
    
    // 按predicate分组
    map<string, vector<Triple>> predicate_groups;
    for (const auto& triple : triples) {
        predicate_groups[triple.predicate].push_back(triple);
    }
    
    // 处理每个predicate组
    for (const auto& group : predicate_groups) {
        const string& predicate = group.first;
        const vector<Triple>& group_triples = group.second;
        
        spdlog::info("处理predicate: {}, 共{}个三元组", predicate, group_triples.size());
        
        // 从LevelDB查询predicate对应的value1
        string value1;
        status = db->Get(leveldb::ReadOptions(), predicate, &value1);
        
        // 输出查询结果
        if (status.ok()) {
            spdlog::info("从LevelDB查询: key={}, value1={}", predicate, value1);
        } else {
            spdlog::info("从LevelDB查询: key={}, 未找到记录", predicate);
        }
        
        if (update_type == UpdateType::INSERT) {
            // 批量插入
            if (value1.empty()) {
                // value1为null，创建新文件并批量插入
                spdlog::info("value1为空，创建新文件并批量插入");
                createNewFileAndUploadBatch(bucket, predicate, group_triples, awsClient, db, result_db);
            } else {
                // value1不为null，追加到现有文件并批量插入
                spdlog::info("value1不为空，追加到现有文件并批量插入");
                appendToExistingFileAndUploadBatch(bucket, value1, group_triples, awsClient, db, result_db);
            }
        } else {
            // 批量删除
            deleteTripleBatch(bucket, predicate, value1, group_triples, awsClient, db);
        }
    }
    
    delete db;
    delete result_db;
    spdlog::info("更新操作完成");
}

// 获取或创建subject的ID
int getOrCreateSubjectId(leveldb::DB* db, leveldb::DB* result_db, const string& subject);

// 获取或创建object的ID
int getOrCreateObjectId(leveldb::DB* db, leveldb::DB* result_db, const string& object);

// Insert操作
void insertTriple(
    const string& bucket,
    const Triple& triple,
    const string& value1,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db,
    leveldb::DB* result_db
) {
    auto start_time = chrono::high_resolution_clock::now();
    vector<Triple> triples = {triple};
    if (value1.empty()) {
        // value1为null，直接新增文件
        spdlog::info("value1为空，创建新文件");
        createNewFileAndUploadBatch(bucket, triple.predicate, triples, awsClient, db, result_db);
    } else {
        // value1不为null，直接追加到现有文件
        spdlog::info("value1不为空，追加到现有文件");
        appendToExistingFileAndUploadBatch(bucket, value1, triples, awsClient, db, result_db);
    }
    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    spdlog::info("Insert操作耗时: {} ms", duration.count());
}

// Delete操作
void deleteTriple(
    const string& bucket,
    const Triple& triple,
    const string& value1,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db
) {
    auto start_time = chrono::high_resolution_clock::now();
    vector<Triple> triples = {triple};
    deleteTripleBatch(bucket, triple.predicate, value1, triples, awsClient, db);
    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    spdlog::info("Delete操作耗时: {} ms", duration.count());
}

// 生成唯一的数字value1
string generateUniqueValue1(leveldb::DB* db) {
    // 从int32最大值开始递减
    long long start_id = 2147483647; // int32最大值
    
    // 尝试不同的步长（更缓慢的增加）
    for (long long current_step = 1; current_step <= 100; current_step += 1) {
        for (int i = 0; i < 1000; i++) {
            long long value1 = start_id - (i * current_step);
            // 确保value1为正数
            if (value1 <= 0) break;
            
            string value1_str = to_string(value1);
            string dummy;
            leveldb::Status status = db->Get(leveldb::ReadOptions(), value1_str, &dummy);
            
            if (!status.ok()) {
                spdlog::info("生成唯一value1成功: {} (步长: {})\n", value1_str, current_step);
                return value1_str;
            }
        }
    }
    
    // 如果上面的方法都失败了，使用随机数
    srand(time(nullptr));
    while (true) {
        long long value1 = 1000000 + (rand() % 9000000);
        string value1_str = to_string(value1);
        
        string dummy;
        leveldb::Status status = db->Get(leveldb::ReadOptions(), value1_str, &dummy);
        
        if (!status.ok()) {
            spdlog::info("生成唯一value1成功: {} (随机)", value1_str);
            return value1_str;
        }
    }
    
    return "";
}

// 生成唯一的整数ID
int generateUniqueId(leveldb::DB* db) {
    // 从int32最大值开始递减
    int start_id = 2147483647; // int32最大值
    
    // 尝试不同的步长（更缓慢的增加）
    for (int current_step = 1; current_step <= 100; current_step += 1) {
        for (int i = 0; i < 1000; i++) {
            int id = start_id - (i * current_step);
            // 确保ID为正数
            if (id <= 0) break;
            
            string id_str = to_string(id);
            string dummy;
            leveldb::Status status = db->Get(leveldb::ReadOptions(), id_str, &dummy);
            
            if (!status.ok()) {
                spdlog::info("找到唯一ID: {} (步长: {})", id, current_step);
                return id;
            }
        }
    }
    
    // 如果上面的方法都失败了，使用随机数
    srand(time(nullptr));
    while (true) {
        int id = 1000000 + (rand() % 9000000);
        string id_str = to_string(id);
        
        string dummy;
        leveldb::Status status = db->Get(leveldb::ReadOptions(), id_str, &dummy);
        
        if (!status.ok()) {
            spdlog::info("找到唯一ID: {} (随机)", id);
            return id;
        }
    }
    
    return -1;
}

// 获取或创建subject的ID
int getOrCreateSubjectId(leveldb::DB* db, leveldb::DB* result_db, const string& subject) {
    string subject_key = subject ;
    string id_str;
    leveldb::Status status = db->Get(leveldb::ReadOptions(), subject_key, &id_str);
    
    if (status.ok()) {
        return stoi(id_str);
    } else {
        int new_id = generateUniqueId(db);
        // 存储新ID到LevelDB，防止重复使用
        db->Put(leveldb::WriteOptions(), to_string(new_id), "used");
        // 存储subject到ID的映射（index数据库）
        db->Put(leveldb::WriteOptions(), subject_key, to_string(new_id));
        // 存储ID到subject的反向映射（result_index数据库）
        if (result_db != nullptr) {
            result_db->Put(leveldb::WriteOptions(), to_string(new_id), subject);
        }
        return new_id;
    }
}

// 获取或创建object的ID
int getOrCreateObjectId(leveldb::DB* db, leveldb::DB* result_db, const string& object) {
    string object_key = object ;
    string id_str;
    leveldb::Status status = db->Get(leveldb::ReadOptions(), object_key, &id_str);
    
    if (status.ok()) {
        return stoi(id_str);
    } else {
        int new_id = generateUniqueId(db);
        // 存储新ID到LevelDB，防止重复使用
        db->Put(leveldb::WriteOptions(), to_string(new_id), "used");
        // 存储object到ID的映射（index数据库）
        db->Put(leveldb::WriteOptions(), object_key, to_string(new_id));
        // 存储ID到object的反向映射（result_index数据库）
        if (result_db != nullptr) {
            result_db->Put(leveldb::WriteOptions(), to_string(new_id), object);
        }
        return new_id;
    }
}

// 创建新文件并上传到S3（批量版本）
void createNewFileAndUploadBatch(
    const string& bucket,
    const string& predicate,
    const vector<Triple>& triples,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db,
    leveldb::DB* result_db
) {
    auto start_time = chrono::high_resolution_clock::now();
    // 生成新的value1（数字格式）
    string value1 = generateUniqueValue1(db);
    
    // 存储value1到LevelDB，防止重复使用
    leveldb::Status status = db->Put(leveldb::WriteOptions(), value1, "used");
    if (!status.ok()) {
        spdlog::error("存储value1到LevelDB失败: {}", status.ToString());
    }
    
    // 创建本地临时CSV文件
    string local_csv_path = "/tmp/" + value1 + ".csv";
    ofstream csv_file(local_csv_path);
    
    if (!csv_file.is_open()) {
        spdlog::error("无法创建CSV文件: {}", local_csv_path);
        return;
    }
    
    // 写入表头
    csv_file << "subject,object\n";
    
    // 批量处理所有三元组
    for (const auto& triple : triples) {
        // 获取或创建subject和object的ID
        int subject_id = getOrCreateSubjectId(db, result_db, triple.subject);
        int object_id = getOrCreateObjectId(db, result_db, triple.object);
        
        // 写入数据（使用ID）
        csv_file << subject_id << "," << object_id << "\n";
        
        spdlog::info("添加三元组: {} {} {} (ID: {}, {})", 
                    triple.subject, triple.predicate, triple.object, 
                    subject_id, object_id);
    }
    
    csv_file.close();
    
    // 上传到S3
    string s3_key = value1 + ".csv";
    if (!uploadFileToS3(bucket, s3_key, local_csv_path, awsClient)) {
        spdlog::error("上传CSV文件失败");
        remove(local_csv_path.c_str());
        return;
    }
    
    // 获取文件大小
    ifstream file(local_csv_path, ios::binary | ios::ate);
    size_t file_size = file.tellg();
    file.close();
    
    // 创建并上传索引文件
    createAndUploadIndexFile(bucket, value1, file_size, awsClient);
    
    // 更新LevelDB，存储predicate到value1的映射
    status = db->Put(leveldb::WriteOptions(), predicate, value1);
    if (!status.ok()) {
        spdlog::error("更新LevelDB失败: {}", status.ToString());
    }
    
    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    
    spdlog::info("新文件创建并上传完成: {}", s3_key);
    spdlog::info("为predicate '{}' 分配的value1: {}", predicate, value1);
    spdlog::info("批量插入完成，共{}个三元组", triples.size());
    spdlog::info("批量插入操作耗时: {} ms", duration.count());
}

// 追加到现有文件并重新上传（批量版本）
void appendToExistingFileAndUploadBatch(
    const string& bucket,
    const string& value1,
    const vector<Triple>& triples,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db,
    leveldb::DB* result_db
) {
    auto start_time = chrono::high_resolution_clock::now();
    string s3_key = value1 + ".csv";
    string local_csv_path = "/tmp/" + value1 + ".csv";
    
    // 下载现有文件
    if (!downloadFileFromS3(bucket, s3_key, local_csv_path, awsClient)) {
        spdlog::error("下载现有文件失败: {}", s3_key);
        return;
    }
    
    // 在文件末尾追加新三元组（使用ID）
    ofstream csv_file(local_csv_path, ios::app);
    if (!csv_file.is_open()) {
        spdlog::error("无法打开CSV文件进行追加: {}", local_csv_path);
        return;
    }
    
    // 批量处理所有三元组
    for (const auto& triple : triples) {
        // 获取或创建subject和object的ID
        int subject_id = getOrCreateSubjectId(db, result_db, triple.subject);
        int object_id = getOrCreateObjectId(db, result_db, triple.object);
        
        // 写入数据（使用ID）
        csv_file << subject_id << "," << object_id << "\n";
        
        spdlog::info("添加三元组: {} {} {} (ID: {}, {})", 
                    triple.subject, triple.predicate, triple.object, 
                    subject_id, object_id);
    }
    
    csv_file.close();
    
    // 重新上传到S3
    if (!uploadFileToS3(bucket, s3_key, local_csv_path, awsClient)) {
        spdlog::error("上传更新后的文件失败");
        return;
    }
    
    // 获取新文件大小
    ifstream file(local_csv_path, ios::binary | ios::ate);
    size_t new_size = file.tellg();
    file.close();
    
    // 更新索引文件
    createAndUploadIndexFile(bucket, value1, new_size, awsClient);
    
    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    
    spdlog::info("文件追加并重新上传完成: {}", s3_key);
    spdlog::info("批量追加完成，共{}个三元组", triples.size());
    spdlog::info("批量追加操作耗时: {} ms", duration.count());
}

// 使用 sed 直接替换（如果匹配行数不多）
void deleteTripleBatch(
    const string& bucket,
    const string& predicate,
    const string& value1,
    const vector<Triple>& triples,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db
) {
    auto start_time = chrono::high_resolution_clock::now();
    if (value1.empty()) {
        spdlog::warn("无法删除：predicate不存在于LevelDB中");
        return;
    }
    
    // 下载现有文件
    string local_csv_path = "/tmp/" + value1 + ".csv";
    string s3_key = value1 + ".csv";
    
    if (!downloadFileFromS3(bucket, s3_key, local_csv_path, awsClient)) {
        spdlog::error("下载文件失败: {}", s3_key);
        return;
    }
    
    // 收集所有要删除的三元组的ID对
    set<pair<string, string>> triples_to_delete;
    for (const auto& triple : triples) {
        int subject_id = getOrCreateSubjectId(db, nullptr, triple.subject);
        int object_id = getOrCreateObjectId(db, nullptr, triple.object);
        triples_to_delete.insert({to_string(subject_id), to_string(object_id)});
    }
    
    if (triples_to_delete.empty()) {
        spdlog::warn("没有要删除的三元组");
        remove(local_csv_path.c_str());
        return;
    }
    
    // 构建 awk 命令
    string temp_file = local_csv_path + ".tmp";
    string awk_cmd = "awk 'BEGIN { FS=OFS=\",\" } ";
    awk_cmd += "NR==1 {print; next} ";

    // 添加删除规则
    for (const auto& pair : triples_to_delete) {
        awk_cmd += "$1==\"" + pair.first + "\" && $2==\"" + pair.second + "\" {";
        awk_cmd += "orig_len=length($0); ";
        awk_cmd += "new_line=\"-1,-1\"; ";
        awk_cmd += "if (orig_len > length(new_line)) {";
        awk_cmd += "    new_line = new_line sprintf(\"%*s\", orig_len - length(new_line), \" \"); ";
        awk_cmd += "}";
        awk_cmd += "print new_line; next; } ";
    }
    awk_cmd += "{print}' " + local_csv_path + " > " + temp_file;
    
    // 执行 awk 命令
    int ret = system(awk_cmd.c_str());
    
    if (ret != 0) {
        spdlog::error("awk 处理失败");
        remove(temp_file.c_str());
        remove(local_csv_path.c_str());
        return;
    }
    
    // 使用 grep 统计删除数量
    string count_cmd = "grep -c '^-1,-1' " + temp_file;
    FILE* pipe = popen(count_cmd.c_str(), "r");
    int found_count = 0;
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            found_count = atoi(buffer);
        }
        pclose(pipe);
    }
    
    if (found_count == 0) {
        spdlog::warn("未找到要删除的三元组");
        remove(temp_file.c_str());
        return;
    }
    
    spdlog::info("找到 {} 个要删除的三元组", found_count);
    
    // 替换原文件
    rename(temp_file.c_str(), local_csv_path.c_str());
    
    // 重新上传文件
    if (!uploadFileToS3(bucket, s3_key, local_csv_path, awsClient)) {
        spdlog::error("上传更新后的文件失败");
        return;
    }
    
    // 获取新文件大小并更新索引
    Aws::S3::Model::HeadObjectRequest headRequest;
    headRequest.SetBucket(bucket);
    headRequest.SetKey(s3_key);
    auto headOutcome = awsClient->HeadObject(headRequest);
    
    if (headOutcome.IsSuccess()) {
        size_t new_size = headOutcome.GetResult().GetContentLength();
        createAndUploadIndexFile(bucket, value1, new_size, awsClient);
    }
    
    // 清理临时文件
    remove(local_csv_path.c_str());
    
    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    
    spdlog::info("批量删除操作完成，共标记{}个三元组，耗时: {} ms", found_count, duration.count());
}
// 从S3下载文件到本地
bool downloadFileFromS3(
    const string& bucket,
    const string& key,
    const string& local_path,
    shared_ptr<Aws::S3::S3Client> awsClient
) {
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(bucket);
    request.SetKey(key);
    
    auto outcome = awsClient->GetObject(request);
    
    if (!outcome.IsSuccess()) {
        // 检查是否是文件不存在的错误
        auto error = outcome.GetError();
        if (error.GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY) {
            // 文件不存在，这是正常情况，返回false
            return false;
        } else {
            // 其他错误，打印错误信息
            spdlog::error("下载文件失败: {}", error.GetMessage());
            return false;
        }
    }
    
    // 保存到本地文件
    ofstream output_file(local_path, ios::binary);
    if (!output_file.is_open()) {
        spdlog::error("无法创建本地文件: {}", local_path);
        return false;
    }
    
    auto& stream = outcome.GetResult().GetBody();
    output_file << stream.rdbuf();
    output_file.close();
    
    spdlog::info("文件下载成功: {} -> {}", key, local_path);
    return true;
}

// 上传文件到S3
bool uploadFileToS3(
    const string& bucket,
    const string& key,
    const string& local_path,
    shared_ptr<Aws::S3::S3Client> awsClient
) {
    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(bucket);
    request.SetKey(key);
    
    // 打开本地文件
    shared_ptr<Aws::IOStream> input_data = 
        Aws::MakeShared<Aws::FStream>("SampleAllocationTag", local_path.c_str(), 
                                      ios::in | ios::binary);
    
    if (!input_data->good()) {
        spdlog::error("无法读取本地文件: {}", local_path);
        return false;
    }
    
    request.SetBody(input_data);
    
    auto outcome = awsClient->PutObject(request);
    
    if (!outcome.IsSuccess()) {
        spdlog::error("上传文件失败: {}", outcome.GetError().GetMessage());
        return false;
    }
    
    spdlog::info("文件上传成功: {} -> {}/{}", local_path, bucket, key);
    return true;
}

// 创建索引文件并上传
void createAndUploadIndexFile(
    const string& bucket,
    const string& predicate,
    size_t file_size,
    shared_ptr<Aws::S3::S3Client> awsClient,
    const string& object_id
) {
    string index_file_path = "/tmp/" + predicate + "_index.csv";
    string s3_index_key = predicate + "_index.csv";
    
    // 尝试下载现有的索引文件
    bool file_exists = downloadFileFromS3(bucket, s3_index_key, index_file_path + ".tmp", awsClient);
    
    if (file_exists && !object_id.empty()) {
        // 使用Arrow库读取现有索引文件
        auto input_stream = arrow::io::ReadableFile::Open(index_file_path + ".tmp").ValueOrDie();
        
        // 读取CSV文件
        auto read_options = arrow::csv::ReadOptions::Defaults();
        auto parse_options = arrow::csv::ParseOptions::Defaults();
        auto convert_options = arrow::csv::ConvertOptions::Defaults();
        
        // 设置列名
        convert_options.column_types = {
            {"object", arrow::utf8()},
            {"start", arrow::int64()},
            {"end", arrow::int64()}
        };
        
        auto table_reader = arrow::csv::TableReader::Make(
            arrow::io::default_io_context(), input_stream, read_options, parse_options, convert_options
        ).ValueOrDie();
        
        auto table = table_reader->Read().ValueOrDie();
        
        // 获取列数据
        auto object_array = std::static_pointer_cast<arrow::StringArray>(table->GetColumnByName("object")->chunk(0));
        auto start_array = std::static_pointer_cast<arrow::Int64Array>(table->GetColumnByName("start")->chunk(0));
        auto end_array = std::static_pointer_cast<arrow::Int64Array>(table->GetColumnByName("end")->chunk(0));
        
        // 创建新的end列数组
        arrow::Int64Builder end_builder;
        int64_t file_size_int64 = static_cast<int64_t>(file_size);
        bool found = false;
        
        for (int64_t i = 0; i < table->num_rows(); i++) {
            string obj = object_array->GetString(i);
            int64_t end = end_array->Value(i);
            
            if (obj == "size") {
                // size行的end值设为0
                if (!end_builder.Append(0).ok()) {
                    spdlog::error("Append failed for size row");
                    return;
                }
            } else if (obj == object_id && !found) {
                // 找到匹配的object ID，更新end列为文件大小
                if (!end_builder.Append(file_size_int64).ok()) {
                    spdlog::error("Append failed for object {}", obj);
                    return;
                }
                found = true;
                spdlog::info("更新索引文件: object={}, end={}", obj, file_size);
            } else {
                // 保持其他行不变
                if (!end_builder.Append(end).ok()) {
                    spdlog::error("Append failed for row {}", i);
                    return;
                }
            }
        }
        
        std::shared_ptr<arrow::Array> new_end_array;
        if (!end_builder.Finish(&new_end_array).ok()) {
            spdlog::error("Finish failed for end_builder");
            return;
        }
        
        // 创建新的start列数组（更新size行的start值）
        arrow::Int64Builder start_builder;
        for (int64_t i = 0; i < table->num_rows(); i++) {
            string obj = object_array->GetString(i);
            int64_t start = start_array->Value(i);
            
            if (obj == "size") {
                // 更新size行的start值为文件大小
                if (!start_builder.Append(file_size_int64).ok()) {
                    spdlog::error("Append failed for size row");
                    return;
                }
            } else {
                // 保持其他行不变
                if (!start_builder.Append(start).ok()) {
                    spdlog::error("Append failed for row {}", i);
                    return;
                }
            }
        }
        
        std::shared_ptr<arrow::Array> new_start_array;
        if (!start_builder.Finish(&new_start_array).ok()) {
            spdlog::error("Finish failed for start_builder");
            return;
        }
        
        // 创建新的schema
        auto new_schema = arrow::schema({
            arrow::field("object", arrow::utf8()),
            arrow::field("start", arrow::int64()),
            arrow::field("end", arrow::int64())
        });
        
        // 创建新的table
        auto new_table = arrow::Table::Make(new_schema, {
            table->GetColumnByName("object")->chunk(0),
            new_start_array,
            new_end_array
        });
        
        // 写入CSV文件
        ofstream index_file(index_file_path);
        if (!index_file.is_open()) {
            spdlog::error("无法创建索引文件: {}", index_file_path);
            return;
        }
        
        // 写入表头
        index_file << "object,start,end\n";
        
        // 写入数据
        auto new_object_array = std::static_pointer_cast<arrow::StringArray>(new_table->GetColumnByName("object")->chunk(0));
        auto new_start_array_final = std::static_pointer_cast<arrow::Int64Array>(new_table->GetColumnByName("start")->chunk(0));
        auto new_end_array_final = std::static_pointer_cast<arrow::Int64Array>(new_table->GetColumnByName("end")->chunk(0));
        
        for (int64_t i = 0; i < new_table->num_rows(); i++) {
            index_file << new_object_array->GetString(i) << ","
                     << new_start_array_final->Value(i) << ","
                     << new_end_array_final->Value(i) << "\n";
        }
        
        index_file.close();
        remove((index_file_path + ".tmp").c_str());
        
    } else if (file_exists) {
        // 没有提供object_id，只更新size行
        ifstream infile(index_file_path + ".tmp");
        ofstream index_file(index_file_path);
        
        if (!index_file.is_open()) {
            spdlog::error("无法创建索引文件: {}", index_file_path);
            return;
        }
        
        string line;
        bool header_written = false;
        
        while (getline(infile, line)) {
            if (!header_written) {
                index_file << line << "\n";
                header_written = true;
            } else {
                istringstream stream(line);
                string object, start, end;
                if (getline(stream, object, ',') && 
                    getline(stream, start, ',') && 
                    getline(stream, end, ',')) {
                    if (object == "size") {
                        index_file << "size," << file_size << ",0\n";
                    } else {
                        index_file << line << "\n";
                    }
                } else {
                    index_file << line << "\n";
                }
            }
        }
        infile.close();
        index_file.close();
        remove((index_file_path + ".tmp").c_str());
        
    } else {
        // 创建新的索引文件
        ofstream index_file(index_file_path);
        if (!index_file.is_open()) {
            spdlog::error("无法创建索引文件: {}", index_file_path);
            return;
        }
        
        index_file << "object,start,end\n";
        index_file << "size," << file_size << ",0\n";
        index_file.close();
    }
    
    // 上传到S3
    uploadFileToS3(bucket, s3_index_key, index_file_path, awsClient);
    
    // 清理临时文件
    remove(index_file_path.c_str());
}

// 检查三元组是否已存在
bool tripleExists(
    const string& bucket,
    const string& predicate,
    const Triple& triple,
    shared_ptr<Aws::S3::S3Client> awsClient,
    leveldb::DB* db
) {
    // 检查LevelDB中是否存在subject和object的映射
    string subject_key = triple.subject ;
    string object_key = triple.object ;
    
    string subject_value, object_value;
    leveldb::Status s1 = db->Get(leveldb::ReadOptions(), subject_key, &subject_value);
    leveldb::Status s2 = db->Get(leveldb::ReadOptions(), object_key, &object_value);
    
    return s1.ok() && s2.ok() && !subject_value.empty() && !object_value.empty();
}
