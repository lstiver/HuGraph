#include "translate.h"

mutex writeMutex;// 互斥锁，用于文件写入保护

bool compareByTime(const QueryInfo& a, const QueryInfo& b) {
    return a.time < b.time;
}

pair<vector<string>, vector<vector<string>>> get_query(string file_path){
  string dbPath = "/root/S3/index";

  // 打开levedb
  leveldb::DB* db;
  leveldb::Options options;
  options.create_if_missing = false;
  leveldb::Status status = leveldb::DB::Open(options, dbPath, &db);
  if (db == nullptr) {
    spdlog::error("Database object is null.");
    exit(1);
}
  ifstream file(file_path);
  string line;
  vector<string> column;
  vector<vector<string>> query_result;
  int index = 0;
  if (file.is_open()) {
    /*这段代码是取出查询列名的*/
    getline(file,line);
    istringstream stream(line);
    string word;
    if (stream >> word) {// 跳过第一个单词
      while (stream >> word) {// 开始读取剩下的单词
        column.emplace_back(word);
      }
    }
    getline(file, line);

    /*对剩下的子查询做分解*/
    while (getline(file, line)) {
      int length = line.length();
      if(length<=2){
        break;
      }
      line.erase(line.length()-2,2);  //消除每一行的' .'
      istringstream stream(line);
      vector<string>tmp; 
      int i=0;
      bool flag1 = false;
      bool flag2 = false;
      while (stream >> word) {
        if(!word.empty() && word[0] == '?'){
          if(i == 0) {
            flag1 = true;
          } else{
            flag2 = true;
          }
        } else{
          word = word.substr(1, word.length() - 2);
          string t;
          status = db->Get(leveldb::ReadOptions(), word, &t); 
          if (status.ok()) {
            word = t;
          } else {
            word = "<"+word+">";
            status = db->Get(leveldb::ReadOptions(), word, &t); 
            if (status.ok()) {
              word = t;
            } else {
              cerr << "Key:" << word <<" not found." << endl;
              exit(1);
            }
            
          }
        }
        tmp.emplace_back(word);//存的顺序是subject，predicate，object
        i++;
      }
      string sql;
      if(flag1&&flag2){
        sql = "SELECT * FROM s3object s";
        tmp.emplace_back(string());
      } else if(flag1){
        sql = "SELECT s.subject FROM s3object s WHERE s.object = '" + tmp[2] + "'";
        tmp.emplace_back(tmp[2]);
      } else {
        sql = "SELECT s.subject FROM s3object s WHERE s.subject = '" + tmp[0] + "'";
        tmp.emplace_back(tmp[0]);
      }
      tmp.emplace_back(sql);
      //存入sql子查询
      query_result.emplace_back(tmp);
    }
    file.close(); //关闭文件
    delete db;
    return {column,query_result};
  } else {
    delete db;
    perror("打开文件失败");
    exit(1);
  }
}

vector<QueryInfo> getTimeAndCost(const string &bucket, 
                                           const vector<string> & row, 
                                           int index, 
                                           shared_ptr<OssClient> ossclient){
  string key = row[1] + "_index.csv";
  size_t index_size = 0;//index文件大小
  auto outcome = ossclient->GetObjectMeta(bucket, key);
    
    if (outcome.isSuccess()) {
        // 获取文件大小
        auto result = outcome.result();
        index_size = result.ContentLength();
        cout << index_size << endl;
    } else {
        cerr << "Error: " << outcome.error().Code() << ", Message: " << outcome.error().Message() << endl;
    }
  auto[size,start,end] = getRangebyget(bucket,key,row[3],ossclient);
  size_t total=0; //用来记录查询totallength
  vector<QueryInfo>time_and_cost;
  // 根据获得的size，start，end来估算cost和time
  if(end == 0){
    total = size;
  } else {
    total = end -start;
  }
  double totallength = static_cast<float>(total)/1024/1024/1024;

  QueryInfo query1;
  query1.subject = row[0];
  query1.object = row[2];
  query1.keyName = row[1];
  query1.size = size;
  query1.start = start;
  query1.end = end;
   
  query1.method= 1;  // Using integer values for keys to represent "getObject"
  query1.time = size*0.0000130980;
  query1.cost = size*0.0000130980*0.96/3600;
  query1.index = index;
  time_and_cost.emplace_back(query1);

  // query1.method= 2;  // Using integer values for keys to represent "selectINDEX+selectDATA"
  // query1.time = 0.0000092584*(index_size+total);
  // // query1.cost = totallength * (0.002 + 0.0007) + 0.002 * totallength;
  // query1.cost = 0.0000092584*(index_size+total)*0.96/3600+0.002*((index_size/1024/1024/1024)+totallength)+0.0007*totallength;
  // if (end > 0) {
  //     time_and_cost.emplace_back(query1);
  // }

  // query1.method= 3;  // Using integer values for keys to represent "s3Select"
  // query1.time = size* 0.0000092584;
  // // query1.cost = totallength * 0.0007 * 0.96/3600 + 0.002 * (size/1024/1024/1024);
  // query1.cost = size* 0.0000092584*0.96/3600 + 0.002* (size/1024/1024/1024);
  // time_and_cost.emplace_back(query1);

  query1.method= 4;  // Using integer values for keys to represent "selectINDEX+getobjectDATA"
  query1.time = 0.0000092584*index_size+0.0000130980*total;
  query1.cost = (0.0000092584*index_size+0.0000130980*total)*0.96/3600+0.002*(index_size/1024/1024/1024);
  if (end > 0) {
      time_and_cost.emplace_back(query1);
  }

  query1.method= 5;  // Using integer values for keys to represent "getobjectINDEX+getobjectDATA"
  query1.time = 0.0000130980*(total+index_size);
  query1.cost = 0.0000130980*(total+index_size)*0.96/3600;
  if (end > 0) {
      time_and_cost.emplace_back(query1);
  }

  // query1.method= 6;  // Using integer values for keys to represent "getobjectINDEX+selectDATA"
  // query1.time = 0.0000130980*index_size+0.0000092584*total;
  // query1.cost = (0.0000130980*index_size+0.0000092584*total)*0.96/3600+(0.002+0.0007)*totallength;
  // if (end > 0) {
  //     time_and_cost.emplace_back(query1);
  // }
   for (const auto& query : time_and_cost) {
        cout << "key: " << query.method << ", time: " << query.time << ", cost: " << query.cost << ", index: " << query.index << endl;
    }
    cout << "Total: " << total << endl;
  return time_and_cost;;
}

// Function to write a vector of integers to a CSV file
void writeVectorToCSV(ofstream &csvFile, const vector<string>& resultVector) {
    for (const auto& value : resultVector) {
        csvFile << value << ",";
    }
    csvFile << "\n";
}

void printResult(const shared_ptr<arrow::Table> table) {
  int64_t num_columns = table->num_columns();
  int64_t num_rows = table->num_rows();
  string dbPath = "/root/S3/index";

  // 打开levedb
  leveldb::DB* db;
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::Status status = leveldb::DB::Open(options, dbPath, &db);
  if (!status.ok()) {
    spdlog::error("Failed to open LevelDB: {}", status.ToString());
    exit(1);
}
  if (db == nullptr) {
    spdlog::error("Database object is null.");
    exit(1);
}
for (int64_t i = 0; i < num_columns; ++i){
  auto column_name = table->field(i)->name();
  cout<<column_name<<" ";
}
cout<<endl;
// 遍历每一行
for (int64_t row = 0; row < num_rows; ++row) {
    for (int64_t i = 0; i < num_columns; ++i) {
        // 获取每一列
        auto column = table->column(i);
        
        // 确定 row 所在的 chunk
        int64_t row_in_chunk = row;
        for (int64_t chunk_index = 0; chunk_index < column->num_chunks(); ++chunk_index) {
            auto chunked_array = column->chunk(chunk_index);

            // 检查 row_in_chunk 是否在当前 chunk 的范围内
            if (row_in_chunk < chunked_array->length()) {
                // 获取标量值
                auto result = chunked_array->GetScalar(row_in_chunk);
                
                if (result.ok()) {
                    auto scalar = result.ValueOrDie();
                    string t;
                    auto status = db->Get(leveldb::ReadOptions(), scalar->ToString(), &t); 
                    // 打印列名和该行值
                    cout << t << " ";
                } else {
                    cerr << "Error: " << result.status().ToString() << endl;
                }
                break;  // 找到对应 chunk，跳出循环
            } else {
                // row_in_chunk 不在当前 chunk 中，减去当前 chunk 的长度，继续查找
                row_in_chunk -= chunked_array->length();
            }
        }
    }
    cout<<endl;
  }
  delete db;
}
