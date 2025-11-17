#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <iterator>
#include <functional>
#include <leveldb/db.h>
#include "translate.h"
#include "sort.h"
#include "query.h"
#include "merge.h"
#include "order.h"
#include <chrono>
#include <memory>
#include <alibabacloud/oss/OssClient.h>

using chrono::high_resolution_clock;
using chrono::milliseconds;
using namespace AlibabaCloud::OSS;
using namespace std;

void ExeQuery(string query_name, shared_ptr<OssClient> ossclient);
int index_ = 0;
vector<string>col1;
vector<string>col2;
string bucket = "watdiv100m";
int totalTime = 45870000;  // 最大时间

int main() {
  string str;
   std::string Endpoint = "oss-cn-wuhan-lr.aliyuncs.com";
    std::string Region = "cn-wuhan-lr";
    std::string BucketName = "watdiv100m";
    
    /* 从环境变量读取AccessKey和Secret */
    const char* AccessKeyId = std::getenv("OSS_ACCESS_KEY_ID");
    const char* AccessKeySecret = std::getenv("OSS_ACCESS_KEY_SECRET");

    /* 初始化网络等资源。*/
    InitializeSdk();
    arrow::compute::Initialize();

    ClientConfiguration conf;
    conf.signatureVersion = SignatureVersionType::V4;
    
    /* 使用手动设置的凭证创建Client */
    auto credentials = std::make_shared<SimpleCredentialsProvider>(AccessKeyId, AccessKeySecret);
    OssClient client(Endpoint, credentials, conf);
    client.SetRegion(Region);
    auto ossclient = std::make_shared<OssClient>(client);
  {
    while(true){
      cout<<"请输入查询数据集(输入exit结束程序):"<<endl;
      cin>>str;
      if(str == "exit") {
        break;
      } else {
        bucket = str;
      }
      cout<<"请输入查询名称(输入exit结束程序):"<<endl;
      cin>>str;
      if(str == "exit") {
        break;
      }
      ExeQuery(str, ossclient);
      // testindex(s3Client);
      // test(s3Client);
   }
    ShutdownSdk();
    spdlog::info("S3 Client连接断开,结束程序");
  }
    return 0;
}

void ExeQuery(string query_name, shared_ptr<OssClient> ossclient){
  // const string file_path = "/home/ec2-user/s3/S3C++/queries/wikidata/" + query_name + ".rq";
  const string file_path = "/root/S3/queries/new_query/" + query_name + ".txt";
  // const string file_path = "/home/ec2-user/s3/S3C++/queries/" + query_name + ".rq";
  high_resolution_clock::time_point beginTime = high_resolution_clock::now();

  //获得分解后的子查询
  auto [column_name,query_result] = get_query(file_path); 
  int index = 0;
  vector<vector<QueryInfo>>selectQuery;
  for (const auto& row : query_result) {
    auto timeAndCost = getTimeAndCost(bucket, row, index,ossclient);
    selectQuery.emplace_back(std::move(timeAndCost));
    index++;
    cout <<"第"<<index<<"个子查询"<<endl;
  }
  //计算排列组合所有的可能情况
  auto perms = Descarte(std::move(selectQuery));

  double Cost = numeric_limits<double>::infinity();
  bool timeflag = false;
  unique_ptr<vector<QueryInfo>> min = nullptr;
  // 计算每种组合的总时间和总成本，选择小于totaltime的组合中cost最少的组合
  for (auto& perm : perms) {
    double queryTime = 0;
    double queryCost = 0;
    for (const auto& it : perm) {
      if (it.time > 0) {
        queryTime += it.time;
        queryCost += it.cost;
      } else {
      // 组合中有一个负时间值，跳出当前组合
        queryTime = queryCost = 0;
        break;
      }
    }
    if(queryTime < totalTime){
      timeflag = true;
      if(queryCost <= Cost){
        Cost = queryCost;
        min = make_unique<vector<QueryInfo>>(std::move(perm));
      }
    }
  }
  sort(min->begin(), min->end(), compareByTime);
  getOrder(min);
  
  shared_ptr<arrow::Table> result;
  set<string> tag;
  string subject,object;
  bool flag = false;
  if (timeflag) {
    index_ = 0;
    if (min != nullptr) {
      for (const auto& it : *min){
        cout<<"==================================query"<<index_<<"=================================="<<endl;
        subject = it.subject;
        object = it.object;

        if(subject.find("?") != string::npos && tag.count(subject) > 0){
            col1.emplace_back(subject);
            col2.emplace_back("subject");
        } else {
            col2.emplace_back(subject);
        }
          
        if(object.find("?") != string::npos && tag.count(object) > 0){
            col1.emplace_back(object);
            col2.emplace_back("object");
        } else {
          col2.emplace_back(object);
        }
                
        int method = it.method;
        // int method =2;
        string keyName;
        keyName = it.keyName +".csv"; //排序后这里要修改
        cout<<"第"<<index_+1<<"个查询"<<keyName<<endl;
        cout<<"使用方法"<<method<<endl;
        high_resolution_clock::time_point begin = high_resolution_clock::now();
        switch(method){
          //getObject
          case 1:{
            auto temp = getObject(bucket, keyName, ossclient, col2, it.size);
            if(index_ == 0){
              result = temp;
              flag = true;
            } else {
              result = merge(result, temp, col1,col2);
            }
            spdlog::info("Numbers of result: {}", result->num_rows());
          high_resolution_clock::time_point overallEnd = high_resolution_clock::now();
          milliseconds overallTime = chrono::duration_cast<milliseconds>(overallEnd - begin);
          cout<<"getObject总耗时："<<overallTime.count()<<"ms"<<endl;
          break;  
          }
          // //s3SelectIndex
          case 2:
          case 6:{
          auto temp = s3SelectbyIndex(bucket, keyName, ossclient, col2, it.start, it.end);
            // 使用迭代器循环删除不包含'?'的元素
            if(index_ == 0){
              result = temp;
            } else {
              for (auto it = col2.begin(); it != col2.end();) {
                 if (*it != "subject" && *it != "object" && it->find("?") == std::string::npos) {
                  it = col2.erase(it);  // 删除元素并更新迭代器
                } else {
                  ++it;  // 如果符合条件移动到下一个元素
                }
              }
              result = merge(result, temp, col1,col2);
            }
            high_resolution_clock::time_point overallEnd = high_resolution_clock::now();
            milliseconds overallTime = chrono::duration_cast<milliseconds>(overallEnd - begin);
            cout<<"s3SelectbyIndex总耗时："<<overallTime.count()<<"ms"<<endl;
            break;
          }
          //s3Select
          case 3:{
            // high_resolution_clock::time_point begin = high_resolution_clock::now();
            auto temp = s3Select(bucket, keyName, ossclient, col2);
            // 使用迭代器循环删除不包含'?'的元素
            if(index_ == 0){
              result = temp;
            } else {
              for (auto it = col2.begin(); it != col2.end();) {
                 if (*it != "subject" && *it != "object" && it->find("?") == std::string::npos) {
                  it = col2.erase(it);  // 删除元素并更新迭代器
                } else {
                  ++it;  // 如果符合条件移动到下一个元素
                }
              }
              result = merge(result, temp, col1,col2);
            }
            high_resolution_clock::time_point overallEnd = high_resolution_clock::now();
            milliseconds overallTime = chrono::duration_cast<milliseconds>(overallEnd - begin);
            cout<<"s3Select总耗时："<<overallTime.count()<<"ms"<<endl;
            break;
          }
          //getObjectByIndex
          case 4:
          case 5:{
            cout<<it.start<<" "<<it.end<<endl;
           auto temp = getObjectbyIndex(bucket, keyName, ossclient, col2, it.start, it.end);
            if(index_ == 0){
              result = temp;
              flag = true;
            } else {
              result = merge(result, temp, col1,col2);
            }
            spdlog::info("Numbers of result: {}", result->num_rows());
          high_resolution_clock::time_point overallEnd = high_resolution_clock::now();
          milliseconds overallTime = chrono::duration_cast<milliseconds>(overallEnd - begin);
          cout<<"getObjectbyIndex总耗时："<<overallTime.count()<<"ms"<<endl;
          }
            break;
          default:
            spdlog::error("query",index,"Error!");
        }
        string write_path = "../res/"+ to_string(index_) + ".csv";
        index_++;
        if(subject.find("?") != string::npos && tag.count(subject) == 0){
          tag.insert(subject);
          
        }
        if(object.find("?") != string::npos && tag.count(object)==0){
          tag.insert(object);
        }
        cout<<endl;
        col1.clear();
        col2.clear();
      }
    }
  } else {
      cout << "No valid combinations found" << endl;
  }

  if(index_ == 1 && flag) {
    if(subject.find("?") == string::npos) {
      result = filter(result,subject,object);
    } else if(object.find("?") == string::npos){
      result = filter(result,object,subject);
    }
  }
  int number = 0;
  if(result != nullptr) {
    number = result->num_rows();
  }
  index = result->num_columns();
  vector<int>columns;
  columns.reserve(index);
  auto colname = result->ColumnNames();
  for(const string& col : column_name){
    for(int i =0;i < index; i++){
      if(col == colname[i]) {
        columns.emplace_back(i);
      }
    }
  }
  result = result->SelectColumns(columns).ValueOrDie();
  spdlog::info("Numbers of result: {}", number);
  high_resolution_clock::time_point endTime = high_resolution_clock::now();
  milliseconds timeInterval = chrono::duration_cast<milliseconds>(endTime - beginTime);
  printResult(result);
  cout<<"总耗时："<<timeInterval.count()<<"ms"<<endl;
}