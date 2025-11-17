#ifndef ORDER_H
#define ORDER_H

#include <iostream>
#include <vector>
#include <string>
#include <set>
#include "translate.h"

using namespace std;

void getOrder(unique_ptr<vector<QueryInfo>>& min){
    vector<QueryInfo> doQueriesData;
    doQueriesData.emplace_back((*min)[0]); // 第一个查询直接添加
    vector<QueryInfo> un_read((*min).begin() + 1, (*min).end()); // 剩下的查询
    set<string>queryTag;
    string subject = (*min)[0].subject;
    string object = (*min)[0].object;
    if(subject.find("?") != string::npos) queryTag.insert(subject);
    if(object.find("?") != string::npos) queryTag.insert(object);

    size_t index = (*min).size()-1;
    // 遍历查询
    for (; index > 0; --index) {
        bool matched = false;

        // 遍历剩下的查询
        for (size_t j = 0; j < index; ++j) {
            set<string> intersection_result;
            QueryInfo& other_query = un_read[j];
            subject = other_query.subject;
            object = other_query.object;
            set<string> other_query_variables;
            if(subject.find("?") != string::npos) other_query_variables.insert(subject);
            if(object.find("?") != string::npos) other_query_variables.insert(object);
            set_intersection(queryTag.begin(), queryTag.end(), 
                             other_query_variables.begin(), other_query_variables.end(),
                             inserter(intersection_result, intersection_result.begin()));

            // 完全相同的变量集
            if (other_query_variables == queryTag) {
                queryTag.insert(other_query_variables.begin(), other_query_variables.end());
                doQueriesData.emplace_back(other_query);
                un_read.erase(un_read.begin() + j);  // 尽量减少 erase 操作
                matched = true;
                break;
            }
            // 子集判断，检查是否是当前集合的子集
            else if (includes(queryTag.begin(), queryTag.end(), other_query_variables.begin(), other_query_variables.end())) {
                // queryTag.insert(other_query_variables.begin(), other_query_variables.end());
                doQueriesData.emplace_back(other_query);
                un_read.erase(un_read.begin() + j);
                matched = true;
                break;
            }
            // 交集判断
            else if (!intersection_result.empty()) {
                queryTag.insert(other_query_variables.begin(), other_query_variables.end());
                doQueriesData.emplace_back(other_query);
                un_read.erase(un_read.begin() + j);
                matched = true;
                break;
            }
        }

        // 如果没有匹配到查询，跳过该次循环
        if (!matched) {
          cout<<"未匹配到查询"<<endl;
          continue;
        }
    }

    // 打印结果
    min->clear();
    
    // 将 doQueriesData 中的内容移动到 min 中
    min->assign(std::make_move_iterator(doQueriesData.begin()),
                std::make_move_iterator(doQueriesData.end()));
    
}
#endif