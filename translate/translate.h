// translate.h
#ifndef TRANSLATE_H
#define TRANSLATE_H

#include <iostream>
#include <regex>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <mutex>
#include <queue>
#include <memory>
#include <atomic>
#include <thread>
#include <spdlog/spdlog.h>
#include <leveldb/db.h>
#include <arrow/array.h>
#include <alibabacloud/oss/OssClient.h>
#include "query.h"
using namespace std;
using namespace AlibabaCloud::OSS;

struct QueryInfo {
    double time;
    double cost;
    int index;
    int method; 
    size_t start;
    size_t end;
    size_t size;
    string keyName;
    string object;
    string subject;
    bool operator<(const QueryInfo& other) const {
        return method < other.method; // 使用 std::string 的 operator<
    }
};

bool compareByTime(const QueryInfo& a, const QueryInfo& b);
pair<vector<string>, vector<vector<string>>> get_query(string file_path);
vector<QueryInfo> getTimeAndCost(const string &bucket, 
                                            const vector<string> &row, 
                                            int index, 
                                            std::shared_ptr<OssClient> ossclient);
void writeVectorToCSV(ofstream &csvFile, const vector<int>& vec);
void printResult(shared_ptr<arrow::Table> result);

#endif 
