#ifndef QUERY_H
#define QUERY_H
#include <aws/core/Aws.h>
#include <aws/transfer/TransferManager.h>
#include <aws/transfer/TransferHandle.h>
#include <aws/s3/S3Client.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/core/utils/stream/PreallocatedStreamBuf.h>
#include <aws/core/utils/Array.h>
#include <iostream>
#include <vector>
#include <string>
#include <array>
#include <chrono> 
#include <spdlog/spdlog.h>
#include <aws/core/auth/AWSAuthSigner.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/core/utils/ratelimiter/DefaultRateLimiter.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/InputSerialization.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/SelectObjectContentRequest.h>
#include <aws/s3/model/CSVInput.h>
#include <aws/s3/model/CSVOutput.h>
#include <aws/s3/model/InputSerialization.h>
#include <aws/s3/model/OutputSerialization.h>
#include <aws/s3/model/RecordsEvent.h>
#include <aws/s3/model/StatsEvent.h>
#include <aws/core/utils/logging/LogMacros.h>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/csv/api.h>
#include "ArrowInputStream.h"
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <arrow/ipc/writer.h>
#include <arrow/csv/api.h>
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
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/acero/exec_plan.h>
#include <arrow/compute/api.h>
#include <arrow/compute/api_vector.h>
#include <arrow/compute/cast.h>
#include <arrow/compute/expression.h>
#include <iostream>
#include <memory>
#include <utility>
using namespace std;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
using namespace Aws::S3;
using namespace Aws::S3::Model;
using namespace Aws::Utils;
using namespace Aws::Transfer;
namespace cp = ::arrow::compute;
namespace ac = ::arrow::acero;

static const int CSV_READER_BUFFER_SIZE = 128 * 1024;

shared_ptr<arrow::Table> getObject(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> & col,
    size_t length);

shared_ptr<arrow::Table> getObject(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> &col);
    
shared_ptr<arrow::Table> getObjectbyIndex(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> & col,
    size_t start,
    size_t end);
    
shared_ptr<arrow::Table> s3Select(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> & col);

shared_ptr<arrow::Table> s3SelectbyIndex(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> & col,
    size_t start,
    size_t end);

array<size_t, 3> getRange(const string &bucket, 
                       const string &key,
                       const string &parsed_conditions,
                       shared_ptr<Aws::S3::S3Client> awsClient);
                       
array<size_t, 3> getRangebyget(const string &bucket, 
                       const string &key,
                       const string &parsed_conditions,
                       shared_ptr<Aws::S3::S3Client> awsClient);  
                       
shared_ptr<arrow::Table> getobjectfilter(shared_ptr<arrow::Table> table, 
                                string col1,
                                string col2,
                                vector<size_t> fil);                     
#endif