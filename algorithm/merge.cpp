#include "merge.h"
#include <arrow/array.h>
#include <arrow/builder.h>

#include <arrow/acero/exec_plan.h>
#include <arrow/compute/api.h>
#include <arrow/compute/api_vector.h>
#include <arrow/compute/cast.h>
#include <arrow/compute/expression.h>

#include <arrow/csv/api.h>

#include <arrow/io/interfaces.h>
#include <arrow/io/memory.h>

#include <arrow/result.h>
#include <arrow/status.h>
#include <arrow/table.h>

#include <arrow/ipc/api.h>

#include <arrow/util/future.h>
#include <arrow/util/range.h>
#include <arrow/util/thread_pool.h>
#include <arrow/util/vector.h>

mutex result_mutex; // 互斥锁，用于保护结果集
mutex dataA_mutex;      // 互斥锁，用于保护 dataA 访问
mutex bloomFilter_mutex;// 互斥锁，用于保护布隆过滤器访问
extern int index_;

std::shared_ptr<arrow::Table> merge(
    std::shared_ptr<arrow::Table> dataA,
    std::shared_ptr<arrow::Table> dataB,
    const std::vector<std::string>& col1,
    const std::vector<std::string>& col2) 
{
    // std ::cout << "dataA address: " << dataA.get() << std::endl;
    // std::cout << "dataB address: " << dataB.get() << std::endl;

    if(!dataA || !dataB) {
        spdlog::debug("结果为空");
        return nullptr;
    }
    // std::cout << "last schema: " << dataA->schema()->ToString() << std::endl;
    // std::cout << "new schema: " << dataB->schema()->ToString() << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<arrow::FieldRef> last_left_refs;
    arrow::compute::Expression filter_condition = arrow::compute::literal(true);
    for (int i = 0; i < dataA->schema()->num_fields(); ++i) {
        std::string col_name = dataA->schema()->field(i)->name();
        if(col_name.find("?") == string::npos) {
            filter_condition = arrow::compute::equal(
            arrow::compute::field_ref(col_name),
            arrow::compute::literal((static_cast<size_t>(std::stoull(col_name))))
            );
        } else {
            last_left_refs.emplace_back(arrow::FieldRef(col_name));
            // cout<<"last_col:"<<col_name<<endl;
        }
    }

    int max_batch_size = 20000;
    if (max_batch_size <= 0) {
        spdlog::error("max_batch_size must be greater than 0.");
        return nullptr;
    }

    auto table_source_left = arrow::acero::TableSourceNodeOptions{dataA, max_batch_size};
    auto table_source_right = arrow::acero::TableSourceNodeOptions{dataB, max_batch_size};
    spdlog::info("Created table source options successfully.");

    arrow::acero::Declaration left{"table_source", std::move(table_source_left)};
    arrow::acero::Declaration right{"table_source", std::move(table_source_right)};
    spdlog::info("Created left and right table declarations successfully.");

    std::vector<arrow::FieldRef> left_refs;
    std::vector<arrow::FieldRef> right_refs;
    for (const auto& col_name : col1) {
        left_refs.emplace_back(arrow::FieldRef(col_name));
        // std::cout << "Left column: " << col_name << std::endl;
    }
    std::vector<arrow::FieldRef> last_right_refs;
    for (const auto& col_name : col2) {
        if(col_name == "object" || col_name == "subject") {
            right_refs.emplace_back(arrow::FieldRef(col_name));
            // std::cout << "Right column: " << col_name << std::endl;
            continue;
        } else if(col_name.find("?") == string::npos) {
            // 创建新的条件
            auto new_condition = arrow::compute::equal(
                arrow::compute::field_ref(col_name),
                arrow::compute::literal(static_cast<size_t>(std::stoull(col_name)))
            );
            // 判断 `filter_condition` 是否为初始的 `true`，然后直接设置或合并
            if (filter_condition.Equals(arrow::compute::literal(true))) {
                filter_condition = new_condition;
            } else {
                filter_condition = arrow::compute::and_(filter_condition, new_condition);
            }
        } else {
            last_right_refs.emplace_back(arrow::FieldRef(col_name));
            // cout<<"last_col:"<<col_name<<endl;
        }
    }
    
    // 使用 HashJoinNodeOptions 
    arrow::acero::HashJoinNodeOptions join_opts{
        arrow::acero::JoinType::INNER, 
        left_refs, 
        right_refs,
        last_left_refs,
        last_right_refs,
        // /*filter*/ arrow::compute::literal(true),
        filter_condition,
        /*output_suffix_for_left*/ "_l",
        /*output_suffix_for_right*/ "_r",
    };

    arrow::acero::Declaration hashjoin{
        "hashjoin", {std::move(left), std::move(right)}, std::move(join_opts)};
    spdlog::info("Hash join declaration.");

    // 收集结果到一个 Table
    arrow::Result<std::shared_ptr<arrow::Table>> result = arrow::acero::DeclarationToTable(std::move(hashjoin));
    // 检查是否成功
    if (!result.ok()) {
        spdlog::error("Error during hash join: {}", result.status().ToString());
        return nullptr;
    }

    std::shared_ptr<arrow::Table> response_table = result.ValueOrDie();
    spdlog::info("Number of rows: {}", response_table->num_rows());
    // std::cout << "result schema: " << response_table->schema()->ToString() << std::endl;
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    spdlog::info("merge用时 {} ms", duration.count());
    return response_table;
}
shared_ptr<arrow::Table> filter(shared_ptr<arrow::Table> table, 
                                string col1,
                                string col2)
{
    arrow::dataset::internal::Initialize();
    auto dataset = std::make_shared<arrow::dataset::InMemoryDataset>(table);
    auto options = std::make_shared<arrow::dataset::ScanOptions>();
    cp::Expression filter_expr = arrow::compute::equal(
                arrow::compute::field_ref(col1),
                arrow::compute::literal(static_cast<size_t>(std::stoull(col1)))
            );
    options->filter = filter_expr;
    options->add_augmented_fields = false;
    options->projection = cp::project({arrow::compute::field_ref(col2)}, {col2});
    auto scan_node_options = arrow::dataset::ScanNodeOptions{dataset, options};
    arrow::acero::Declaration scan{"scan", std::move(scan_node_options)};
    spdlog::info("scan declaration.");

    ac::Declaration filter{
      "filter", {std::move(scan)}, ac::FilterNodeOptions(std::move(filter_expr))};

    arrow::Result<std::shared_ptr<arrow::Table>> status = arrow::acero::DeclarationToTable(std::move(filter));
    // 检查是否成功
    if (!status.ok()) {
        spdlog::error("Error during filter: {}", status.status().ToString());
        return nullptr;
    }
    std::shared_ptr<arrow::Table> response_table = status.ValueOrDie();
    spdlog::info("Number of rows: {}", response_table->num_rows());
    // std::cout << "result schema: " << response_table->schema()->ToString() << std::endl;
    return response_table;
}

// shared_ptr<arrow::Table> filterOr(shared_ptr<arrow::Table> table, 
//                                   const string& column_name,
//                                   const std::vector<int32_t>& values)
// {
//     spdlog::info("=== filterOr called ===");
//     spdlog::info("  Column: {}", column_name);
//     spdlog::info("  Filter values: {}", fmt::join(values, ", "));
//     spdlog::info("  Input table rows: {}", table->num_rows());
    
//     arrow::dataset::internal::Initialize();
//     auto dataset = std::make_shared<arrow::dataset::InMemoryDataset>(table);
//     auto options = std::make_shared<arrow::dataset::ScanOptions>();
    
//     if (values.empty()) {
//         spdlog::error("Values vector is empty");
//         return table;
//     }
    
//     // 检查列是否存在
//     auto schema = table->schema();
//     auto field = schema->GetFieldByName(column_name);
//     if (!field) {
//         spdlog::error("Column '{}' not found in schema", column_name);
//         spdlog::info("Available columns:");
//         for (int i = 0; i < schema->num_fields(); i++) {
//             spdlog::info("  - {}", schema->field(i)->name());
//         }
//         return nullptr;
//     }
//     spdlog::info("Column '{}' found, type: {}", column_name, field->type()->ToString());
    
//     // 构建 OR 表达式
//     spdlog::info("Building OR expression for {} values", values.size());
//     cp::Expression filter_expr = arrow::compute::literal(false);
   
//     filter_expr = arrow::compute::equal(
//         arrow::compute::field_ref(column_name),
//         arrow::compute::literal(values[0])
//     );
    
//     for (size_t i = 1; i < values.size(); i++) {
//         cp::Expression condition = arrow::compute::equal(
//             arrow::compute::field_ref(column_name),
//             arrow::compute::literal(values[i])
//         );
//         filter_expr = arrow::compute::or_(filter_expr, condition);
//     }
    
//     spdlog::info("Filter expression created");
//     options->filter = filter_expr;
//     options->add_augmented_fields = false;
    
//     spdlog::info("Creating scan node");
//     auto scan_node_options = arrow::dataset::ScanNodeOptions{dataset, options};
//     arrow::acero::Declaration scan{"scan", std::move(scan_node_options)};
//     spdlog::info("Scan declaration created");

//     spdlog::info("Executing filter...");
//     arrow::Result<std::shared_ptr<arrow::Table>> status = arrow::acero::DeclarationToTable(std::move(scan));
    
//     if (!status.ok()) {
//         spdlog::error("Error during filter: {}", status.status().ToString());
//         return nullptr;
//     }
    
//     std::shared_ptr<arrow::Table> response_table = status.ValueOrDie();
//     spdlog::info("Filter completed: {} -> {} rows", table->num_rows(), response_table->num_rows());
    
//     return response_table;
// }
shared_ptr<arrow::Table> filterOr(shared_ptr<arrow::Table> table, 
                                  const string& column_name,
                                  const std::vector<int32_t>& values)
{
    spdlog::info("=== filterOr called ===");
    spdlog::info("  Column: {}", column_name);
    spdlog::info("  Filter values: {}", fmt::join(values, ", "));
    spdlog::info("  Input table rows: {}", table->num_rows());
    
    if (values.empty()) {
        spdlog::error("Values vector is empty");
        return table;
    }
    
    // 检查列是否存在
    auto schema = table->schema();
    auto field = schema->GetFieldByName(column_name);
    if (!field) {
        spdlog::error("Column '{}' not found in schema", column_name);
        return nullptr;
    }
    
    // 获取列数据
    auto column_chunked = table->GetColumnByName(column_name);
    if (!column_chunked) {
        spdlog::error("Failed to get column '{}'", column_name);
        return nullptr;
    }
    
    // 构建布尔掩码
    std::shared_ptr<arrow::ChunkedArray> mask;
    
    for (size_t i = 0; i < values.size(); i++) {
        // 为当前值创建相等比较
        auto eq_result = arrow::compute::CallFunction("equal", {
            arrow::Datum(column_chunked), 
            arrow::Datum(values[i])
        });
        
        if (!eq_result.ok()) {
            spdlog::error("Equal failed for value {}: {}", 
                         values[i], eq_result.status().ToString());
            return nullptr;
        }
        
        if (i == 0) {
            mask = eq_result.ValueOrDie().chunked_array();
        } else {
            // OR 操作
            auto or_result = arrow::compute::CallFunction("or", {
                arrow::Datum(mask), 
                eq_result.ValueOrDie()
            });
            
            if (!or_result.ok()) {
                spdlog::error("OR failed for value {}: {}", 
                             values[i], or_result.status().ToString());
                return nullptr;
            }
            mask = or_result.ValueOrDie().chunked_array();
        }
    }
    
    // 应用过滤
    auto filter_result = arrow::compute::Filter(
        arrow::Datum(table), 
        arrow::Datum(mask)
    );
    
    if (!filter_result.ok()) {
        spdlog::error("Filter failed: {}", filter_result.status().ToString());
        return nullptr;
    }
    
    auto result_datum = filter_result.ValueOrDie();
    std::shared_ptr<arrow::Table> filtered_table;
    
    // 处理返回结果
    if (result_datum.table()) {
        filtered_table = result_datum.table();
    } else if (result_datum.record_batch()) {
        auto batch = result_datum.record_batch();
        auto table_result = arrow::Table::FromRecordBatches(batch->schema(), {batch});
        if (!table_result.ok()) {
            spdlog::error("Failed to convert batch to table: {}", 
                         table_result.status().ToString());
            return nullptr;
        }
        filtered_table = table_result.ValueOrDie();
    } else {
        spdlog::error("Filter returned unexpected Datum type");
        return nullptr;
    }
    
    spdlog::info("Filter completed: {} -> {} rows", 
                 table->num_rows(), filtered_table->num_rows());
    
    return filtered_table;
}