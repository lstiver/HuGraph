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
#include "update.h"
#include <chrono>
#include <memory>
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>

using chrono::high_resolution_clock;
using chrono::milliseconds;
using namespace Aws::S3;
using namespace Aws::S3::Model;
using namespace std;

void ExeQuery(string query_name, shared_ptr<Aws::S3::S3Client> awsClient);
void showMainMenu();
void handleQueryMode(shared_ptr<Aws::S3::S3Client> s3Client);
void handleUpdateMode(shared_ptr<Aws::S3::S3Client> s3Client);
int index_ = 0;
vector<string>col1;
vector<string>col2;
string bucket = "watdiv100mconvert";
int totalTime = 45870000;  // 最大时间

int main() {
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    Aws::S3::S3ClientConfiguration clientConfig;
    clientConfig.maxConnections = 2000;
    clientConfig.useUSEast1RegionalEndPointOption = Aws::S3::US_EAST_1_REGIONAL_ENDPOINT_OPTION::REGIONAL;
    clientConfig.useVirtualAddressing = true;
    auto s3Client = std::make_shared<Aws::S3::S3Client>(clientConfig);
    {
        string mode;
        while(true) {
            showMainMenu();
            cin >> mode;
            
            if(mode == "exit") {
                break;
            } else if(mode == "1") {
                handleQueryMode(s3Client);
            } else if(mode == "2") {
                handleUpdateMode(s3Client);
            } else {
                cout << "无效的选择，请重新输入" << endl;
            }
        }
        Aws::ShutdownAPI(options);
        spdlog::info("S3 Client连接断开,结束程序");
    }
    return 0;
}

void showMainMenu() {
    cout << "\n========== S3C++ 主菜单 ==========" << endl;
    cout << "请选择操作模式:" << endl;
    cout << "1. 查询数据 (Query)" << endl;
    cout << "2. 更新数据 (Update)" << endl;
    cout << "输入exit结束程序" << endl;
    cout << "==================================" << endl;
    cout << "请输入选项: ";
}

void handleQueryMode(shared_ptr<Aws::S3::S3Client> s3Client) {
    string str;
    cout << "\n--- 查询模式 ---" << endl;
    cout << "请输入查询数据集(输入exit返回主菜单):" << endl;
    cin >> str;
    if(str == "exit") {
        return;
    } else {
        bucket = str;
    }
    cout << "请输入查询名称(输入exit返回主菜单):" << endl;
    cin >> str;
    if(str == "exit") {
        return;
    }
    // testQuery(str,s3Client);
    ExeQuery(str, s3Client);
}

void handleUpdateMode(shared_ptr<Aws::S3::S3Client> s3Client) {
    string str;
    cout << "\n--- 更新模式 ---" << endl;
    cout << "请输入S3存储桶名称(输入exit返回主菜单):" << endl;
    cin >> str;
    if(str == "exit") {
        return;
    }
    string update_bucket = str;
    
    cout << "请输入三元组文件路径(输入exit返回主菜单):" << endl;
    cin >> str;
    if(str == "exit") {
        return;
    }
    string triple_file = str;
    
    cout << "请选择操作类型:" << endl;
    cout << "1. 插入数据 (Insert)" << endl;
    cout << "2. 删除数据 (Delete)" << endl;
    cout << "输入exit返回主菜单" << endl;
    cout << "请输入选项: ";
    
    cin >> str;
    if(str == "exit") {
        return;
    }
    
    UpdateType update_type;
    if(str == "1") {
        update_type = UpdateType::INSERT;
        cout << "执行插入操作..." << endl;
    } else if(str == "2") {
        update_type = UpdateType::DELETE;
        cout << "执行删除操作..." << endl;
    } else {
        cout << "无效的选择，返回主菜单" << endl;
        return;
    }
    
    high_resolution_clock::time_point start = high_resolution_clock::now();
    executeUpdate(update_bucket, triple_file, update_type, s3Client);
    high_resolution_clock::time_point endTime = high_resolution_clock::now();
    milliseconds timeInterval = chrono::duration_cast<milliseconds>(endTime - start);
    spdlog::info("========== Query completed in {} ms ==========", timeInterval.count());
    cout << "update1000rows总耗时：" << timeInterval.count() << "ms" << endl;
}

void ExeQuery(string query_name, shared_ptr<Aws::S3::S3Client> awsClient){
  const string file_path = "/home/ec2-user/s3/S3C++/queries/wikidata/" + query_name + ".rq";
  // const string file_path = "/home/ec2-user/s3/S3C++/queries/new_query/" + query_name + ".txt";
  // const string file_path = "/home/ec2-user/s3/S3C++/queries/" + query_name + ".rq";
  high_resolution_clock::time_point beginTime = high_resolution_clock::now();

  //获得分解后的子查询
  auto [column_name,query_result] = get_query(file_path); 
  int index = 0;
  vector<vector<QueryInfo>>selectQuery;
  for (const auto& row : query_result) {
    auto timeAndCost = getTimeAndCost(bucket, row, index,awsClient);
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
        // int method =1;
        string keyName;
        keyName = it.keyName +".csv"; //排序后这里要修改
        cout<<"第"<<index_+1<<"个查询"<<keyName<<endl;
        high_resolution_clock::time_point begin = high_resolution_clock::now();
        switch(method){
          //getObject
          case 1:{
            auto temp = getObject(bucket, keyName, awsClient, col2, it.size);
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
          //s3SelectIndex
          case 2:
          case 6:{
          auto temp = s3SelectbyIndex(bucket, keyName, awsClient, col2, it.start, it.end);
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
            auto temp = s3Select(bucket, keyName, awsClient, col2);
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
           auto temp = getObjectbyIndex(bucket, keyName, awsClient, col2, it.start, it.end);
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