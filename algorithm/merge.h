#ifndef MERGE_H
#define MERGE_H

#include <iostream>
#include <fstream>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
// #include <aws/core/utils/stream/ResponseStream.h>
#include <arrow/acero/exec_plan.h>
#include <arrow/acero/options.h>
#include <arrow/compute/api.h>
#include <arrow/compute/api_vector.h>
#include <arrow/compute/cast.h>
#include <arrow/compute/api_scalar.h>
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
#include <arrow/dataset/dataset.h>
#include <arrow/dataset/api.h>
#include <arrow/dataset/plan.h>
#include <arrow/dataset/scanner.h>
#include <arrow/dataset/file_base.h>
#include <arrow/result.h>
#include <arrow/dataset/scanner.h>
#include <memory>
#include <utility>
using namespace std;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
namespace cp = ::arrow::compute;
namespace ac = ::arrow::acero;

//对两张表做merge操作，使用arrow::Acero里的hashjoin在hashjoin同时做filter操作
shared_ptr<arrow::Table> merge(shared_ptr<arrow::Table> dataA, 
                               shared_ptr<arrow::Table> dataB, 
                               const vector<string>& col1,
                               const std::vector<string>& col2);

shared_ptr<arrow::Table> filter(shared_ptr<arrow::Table> table, 
                                string col1,
                                string col2);
#endif