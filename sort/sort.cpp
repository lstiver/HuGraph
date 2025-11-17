#include "sort.h"
using namespace std;

vector<vector<QueryInfo>> Descarte( vector<vector<QueryInfo>> sets)
{
    vector<vector<QueryInfo>> result;
    // vector<QueryInfo> tmp;
    // for (const auto& set : sets) {
    //   tmp.push_back(set[0]);
    // }
    // result.push_back(tmp);
    // 初始化结果集合为包含一个空的组合
    result.push_back({});
    for (const auto& set : sets) {
        vector<vector<QueryInfo>> newResult;
        // 对于每个已有的组合，扩展它
        for (const auto& combination : result) {
            for (const auto& element : set) {
                auto newCombination = combination;
                newCombination.push_back(element);
                newResult.push_back(newCombination);
            }
        }
        // 更新结果集合
        result = move(newResult);
    }
    return result;
}