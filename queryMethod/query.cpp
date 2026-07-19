#include "query.h" 

Aws::S3::Model::InputSerialization getInputSerialization() {
    Aws::S3::Model::InputSerialization inputSerialization;
    Aws::S3::Model::CSVInput csvInput;

    csvInput.SetFileHeaderInfo(Aws::S3::Model::FileHeaderInfo::USE);
    csvInput.SetFieldDelimiter(",");
    csvInput.SetRecordDelimiter("\n");
    
    inputSerialization.SetCSV(csvInput);
    return inputSerialization;
}

Aws::S3::Model::OutputSerialization getOutputSerialization() {
    Aws::S3::Model::OutputSerialization outputSerialization;
    Aws::S3::Model::CSVOutput csvOutput;

    csvOutput.SetFieldDelimiter(",");
    csvOutput.SetRecordDelimiter("\n");
    
    outputSerialization.SetCSV(csvOutput);
    return outputSerialization;
}

arrow::Status Write(const std::shared_ptr<arrow::Table>& table, const std::string& file_path) {
    // 创建输出文件流
    auto output_file = arrow::io::FileOutputStream::Open(file_path);
    if (!output_file.ok()) {
        return output_file.status();
    }

    // 创建 CSV Writer
    auto write_options = arrow::csv::WriteOptions::Defaults();
    ARROW_ASSIGN_OR_RAISE(auto writer,
                          arrow::csv::MakeCSVWriter(*output_file, table->schema(), write_options));

    // 写入 Table 数据到 CSV
    ARROW_RETURN_NOT_OK(writer->WriteTable(*table));
    ARROW_RETURN_NOT_OK(writer->Close());
    ARROW_RETURN_NOT_OK(output_file.ValueOrDie()->Close());

    return arrow::Status::OK();
}

shared_ptr<arrow::Table> parseCompletePayload (const Aws::Vector<unsigned char>::iterator &from,
	                                           const Aws::Vector<unsigned char>::iterator &to,
                                               const vector<string> & col
                                               ) 
{
    auto parse_options = arrow::csv::ParseOptions::Defaults();
    auto read_options = arrow::csv::ReadOptions::Defaults();
    read_options.column_names = col; //设置列名
    auto convert_options = arrow::csv::ConvertOptions::Defaults();
    for (const auto& column_name : col) {
        convert_options.column_types[column_name] = arrow::int32();  // 将列设置为 int32 类型
    }
    // if(col.size() == 2){
        // read_options.skip_rows = 1; // 跳过表头
    // }
    // Create a reader
    arrow::io::IOContext ioContext;
    auto reader = std::make_shared<arrow::io::BufferReader>(from.base(), std::distance(from, to));
    auto createResult = arrow::io::BufferedInputStream::Create(CSV_READER_BUFFER_SIZE,
															 arrow::default_memory_pool(),
															 reader,
															 -1);
    if (!createResult.ok()){
        spdlog::error("Cannot parse S3 Select payload  |  Could not create a reader, error: '{}'", createResult.status().message());
        return nullptr;
    }
    auto input = *createResult;

    // Instantiate TableReader from input stream and options
    auto makeReaderResult = arrow::csv::TableReader::Make(ioContext,
													  	  input,
													  	  read_options,
														  parse_options,
														  convert_options);
    if (!makeReaderResult.ok()){
        spdlog::error("Cannot parse S3 Select payload  |  Could not create a table reader, error: '{}'",
		  makeReaderResult.status().message());
        return nullptr;
    }
    auto tableReader = *makeReaderResult;
    auto table = tableReader->Read();
    if(table.ok()){
        // cout<<table.ValueOrDie()->schema()->ToString()<<endl;
        // cout<<table.ValueOrDie()->num_rows()<<endl;
        return table.ValueOrDie();
    } else {
        spdlog::error("读取失败！错误信息: {}", table.status().message());
        return nullptr;
    }
}

class MyUnderlyingStream : public Aws::IOStream
{
    public:
        using Base = Aws::IOStream;
        // Provide a customer-controlled streambuf to hold data from the bucket.
        explicit MyUnderlyingStream(std::streambuf* buf)
            : Base(buf)
        {}

        ~MyUnderlyingStream() override = default;
};


shared_ptr<arrow::Table> getObject(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> & col,
    size_t length)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    auto executor = Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>("executor", std::thread::hardware_concurrency()-1);
    Aws::Transfer::TransferManagerConfiguration transfer_config(executor.get());
    transfer_config.s3Client = awsClient;

    // Create buffer to hold data received by the data stream.
    Aws::Utils::Array<unsigned char> buffer(length);
    cout<<"key:"<<key<<endl;
    cout<<"length:"<<length<<endl;
    Aws::Utils::Stream::PreallocatedStreamBuf streamBuffer(buffer.GetUnderlyingData(), buffer.GetLength());

    auto transfer_manager = TransferManager::Create(transfer_config);
    auto downloadHandle = transfer_manager->DownloadFile(bucket, key, [&]() {
        return Aws::New<MyUnderlyingStream>("TestTag", &streamBuffer);
    });

    downloadHandle->WaitUntilFinished();
    if (downloadHandle->GetStatus() != Aws::Transfer::TransferStatus::COMPLETED) {
        auto err = downloadHandle->GetLastError();
        spdlog::error("File download failed: {}", err.GetMessage());
        return nullptr;
    } else {
        // 将 buffer 转化为 Arrow 的 Buffer
        shared_ptr<arrow::Buffer> arrowBuffer = std::make_shared<arrow::Buffer>(
        buffer.GetUnderlyingData(), buffer.GetLength());

        // 使用 BufferReader 来创建 Arrow 的 InputStream
        shared_ptr<arrow::io::InputStream> inputStream = std::make_shared<arrow::io::BufferReader>(arrowBuffer);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        spdlog::info("转换为 Arrow 的输入流用时 {} ms", duration.count());
        // string buffer_content(reinterpret_cast<const char*>(buffer.GetUnderlyingData()), 
        //                       min(200UL, buffer.GetLength()));
        // spdlog::info("Buffer preview (first 200 bytes):\n{}", buffer_content);

        // 读取 CSV 文件
        auto read_options = arrow::csv::ReadOptions::Defaults();
        read_options.column_names = col; //设置列名
        read_options.skip_rows = 1; // 跳过表头
        
        auto convert_options = arrow::csv::ConvertOptions::Defaults();
        for (const auto& column_name : col) {
            convert_options.column_types[column_name] = arrow::int32();  // 将列设置为 int32 类型
        }
        
        // 自动利用 Arrow 并发进行 CSV 读取
        auto csv_reader = arrow::csv::TableReader::Make(
            arrow::io::default_io_context(), inputStream,
            read_options,
            arrow::csv::ParseOptions::Defaults(),
            convert_options);
        
        if (!csv_reader.ok()) {
            spdlog::error("Failed to create CSV TableReader");
            return nullptr;
        }

        shared_ptr<arrow::csv::TableReader> reader = *csv_reader;
        auto table = reader->Read();
        
        if (table.ok()) {
            auto end_time2 = std::chrono::high_resolution_clock::now();
            auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end_time2 - end_time);
            spdlog::info("转换为table用时 {} ms", duration2.count());
            // spdlog::info("Numbers of result: {}", table.ValueOrDie()->num_rows());
            return table.ValueOrDie();
        } else {
            spdlog::error("转化结果为arrow表格失败: {}", table.status().ToString());
            return nullptr;
        }
    }
} 

shared_ptr<arrow::Table> getObject(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> &col)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 1. 构造GetObject请求
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(bucket);
    request.SetKey(key);
    
    spdlog::info("Fetching object: bucket={}, key={}", bucket, key);
    
    // 2. 执行GetObject调用
    auto get_object_outcome = awsClient->GetObject(request);
    
    if (!get_object_outcome.IsSuccess()) {
        auto error = get_object_outcome.GetError();
        spdlog::error("GetObject failed: {}", error.GetMessage());
        return nullptr;
    }
    
    // 3. 获取响应体（即对象内容）
    auto& s3_stream = get_object_outcome.GetResult().GetBody();
    
    // 4. 获取对象大小
    auto content_length = get_object_outcome.GetResult().GetContentLength();
    spdlog::info("Object size: {} bytes", content_length);
    
    // 5. 读取整个对象到内存缓冲区
    Aws::Utils::Array<unsigned char> buffer(static_cast<size_t>(content_length));
    
    // 检查流是否有效
    if (!s3_stream.good()) {
        spdlog::error("S3 stream is not readable");
        return nullptr;
    }
    
    // 读取数据
    s3_stream.read(reinterpret_cast<char*>(buffer.GetUnderlyingData()), content_length);
    
    auto bytes_read = s3_stream.gcount();
    if (bytes_read != content_length) {
        spdlog::error("Failed to read entire object: read {} bytes out of {}", 
                      bytes_read, content_length);
        return nullptr;
    }
    
    auto download_time = std::chrono::high_resolution_clock::now();
    auto download_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        download_time - start_time);
    spdlog::info("GET Object completed, downloaded {} bytes in {} ms", 
                 bytes_read, download_duration.count());
    
    // 6. 将buffer转化为Arrow的Buffer
    shared_ptr<arrow::Buffer> arrowBuffer = std::make_shared<arrow::Buffer>(
        buffer.GetUnderlyingData(), bytes_read);
    
    // 7. 创建Arrow的InputStream
    shared_ptr<arrow::io::InputStream> inputStream = 
        std::make_shared<arrow::io::BufferReader>(arrowBuffer);
    
    auto convert_start_time = std::chrono::high_resolution_clock::now();
    auto convert_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        convert_start_time - download_time);
    spdlog::info("转换为 Arrow 的输入流用时 {} ms", convert_duration.count());
    
    auto read_options = arrow::csv::ReadOptions::Defaults();
read_options.column_names = {"object", "start", "end"};  // 指定列名
read_options.skip_rows = 2;  // 跳过第一行（原始表头）
read_options.autogenerate_column_names = false;

auto parse_options = arrow::csv::ParseOptions::Defaults();
parse_options.delimiter = ',';

auto convert_options = arrow::csv::ConvertOptions::Defaults();
// 正确设置列类型 - 使用 convert_options.column_types
convert_options.column_types["object"] = arrow::int32();
convert_options.column_types["start"] = arrow::int32();
convert_options.column_types["end"] = arrow::int32();
convert_options.check_utf8 = false;
convert_options.strings_can_be_null = false;

// 创建 CSV reader
auto csv_reader = arrow::csv::TableReader::Make(
    arrow::io::default_io_context(), inputStream,
    read_options, parse_options, convert_options);
    
    if (!csv_reader.ok()) {
        spdlog::error("Failed to create CSV TableReader: {}", csv_reader.status().ToString());
        return nullptr;
    }
    
    shared_ptr<arrow::csv::TableReader> reader = *csv_reader;
    auto table_result = reader->Read();
    
    if (table_result.ok()) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto table_convert_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - convert_start_time);
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        spdlog::info("CSV解析用时 {} ms", table_convert_duration.count());
        spdlog::info("总用时 {} ms", total_duration.count());
        spdlog::info("结果行数: {}", table_result.ValueOrDie()->num_rows());
        
        return table_result.ValueOrDie();
    } else {
        spdlog::error("转化结果为arrow表格失败: {}", table_result.status().ToString());
        return nullptr;
    }
}

shared_ptr<arrow::Table> getObjectbyIndex(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> & col,
    size_t start,
    size_t end)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    auto executor = Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>("executor", std::thread::hardware_concurrency() - 1);
    Aws::Transfer::TransferManagerConfiguration transfer_config(executor.get());
    transfer_config.s3Client = awsClient;

    size_t length = end - start;
    // Create buffer to hold data received by the data stream.
    Aws::Utils::Array<unsigned char> buffer(length);
    Aws::Utils::Stream::PreallocatedStreamBuf streamBuffer(buffer.GetUnderlyingData(), buffer.GetLength());

    auto transfer_manager = TransferManager::Create(transfer_config);
    auto downloadHandle = transfer_manager->DownloadFile(bucket, key, start, length, [&]() {
        return Aws::New<MyUnderlyingStream>("TestTag", &streamBuffer);
    });

    downloadHandle->WaitUntilFinished();
    if (downloadHandle->GetStatus() != Aws::Transfer::TransferStatus::COMPLETED) {
        auto err = downloadHandle->GetLastError();
        spdlog::error("File download failed: {}", err.GetMessage());
        return nullptr;
    } else {
        // 将 buffer 转化为 Arrow 的 Buffer
        shared_ptr<arrow::Buffer> arrowBuffer = std::make_shared<arrow::Buffer>(
        buffer.GetUnderlyingData(), buffer.GetLength());
         // 输出前 200 字节用于调试
//     std::string preview(reinterpret_cast<const char*>(arrowBuffer->data()), 
//                    static_cast<size_t>(std::min(arrowBuffer->size(), static_cast<int64_t>(length))));
// spdlog::info("Buffer preview: {}", preview);
        // 使用 BufferReader 来创建 Arrow 的 InputStream
        shared_ptr<arrow::io::InputStream> inputStream = std::make_shared<arrow::io::BufferReader>(arrowBuffer);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        spdlog::info("转换为 Arrow 的输入流用时 {} ms", duration.count());

        // 读取 CSV 文件
        auto read_options = arrow::csv::ReadOptions::Defaults();
        read_options.column_names = col; //设置列名
        if(start == 0){
            read_options.skip_rows = 1; // 跳过表头
        }
        
        auto convert_options = arrow::csv::ConvertOptions::Defaults();
        for (const auto& column_name : col) {
            convert_options.column_types[column_name] = arrow::int32();  // 将列设置为 int32 类型
        }
        
        // 自动利用 Arrow 并发进行 CSV 读取
        auto csv_reader = arrow::csv::TableReader::Make(
            arrow::io::default_io_context(), inputStream,
            read_options,
            arrow::csv::ParseOptions::Defaults(),
            convert_options);
        
        if (!csv_reader.ok()) {
            spdlog::error("Failed to create CSV TableReader");
            return nullptr;
        }

        shared_ptr<arrow::csv::TableReader> reader = *csv_reader;
        auto table = reader->Read();
        
        if (table.ok()) {
            auto end_time2 = std::chrono::high_resolution_clock::now();
            auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end_time2 - end_time);
            spdlog::info("转换为table用时 {} ms", duration2.count());
            return table.ValueOrDie();
        } else {
            spdlog::error("转化结果为arrow表格失败: {}", table.status().ToString());
            return nullptr;
        }
    }
} 

shared_ptr<arrow::Table> s3SelectbyIndex(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> & col,
    size_t start,
    size_t end)
{
    high_resolution_clock::time_point begin = high_resolution_clock::now();
    const string& subject = col[0];
    const string& object = col[1];
    vector<string>new_col;
    string sql;
    if(subject.find("?") == string::npos && subject != "subject") {
        sql = "SELECT s.object FROM s3object s WHERE s.subject = '" + subject + "'";
        new_col.emplace_back(object);
    } else if(object.find("?") == string::npos && object != "object") {
        sql = "SELECT s.subject FROM s3object s WHERE s.object = '" + object + "'";
        new_col.emplace_back(subject);
    } else {
        sql = "SELECT * FROM s3object s";
        new_col.emplace_back(subject);
        new_col.emplace_back(object);
    }
    cout<<sql<<endl;
    Aws::S3::Model::SelectObjectContentRequest selectObjectContentRequest;
    Aws::S3::Model::ScanRange scanRange;
    // 设置范围
    scanRange.WithStart(start).WithEnd(end);
    // 将范围添加到请求中
    selectObjectContentRequest.SetScanRange(scanRange);
    selectObjectContentRequest.SetBucket(bucket);
    selectObjectContentRequest.SetKey(key);
    selectObjectContentRequest.SetExpressionType(Aws::S3::Model::ExpressionType::SQL);
    selectObjectContentRequest.SetExpression(sql);

    // Input and Output serialization
    selectObjectContentRequest.SetInputSerialization(getInputSerialization());
    selectObjectContentRequest.SetOutputSerialization(getOutputSerialization());

    // Handler to process the response
    Aws::S3::Model::SelectObjectContentHandler handler;
    vector<unsigned char> s3Result_;                         
    bool hasEndEvent = false;

    // Records event callback
    handler.SetRecordsEventCallback([&](const Aws::S3::Model::RecordsEvent &recordsEvent) {
        // cerr << "Record received\n";
        auto payload = recordsEvent.GetPayload();
        if (!payload.empty()) {
            // string buffer_content(reinterpret_cast<const char*>(buffer.GetUnderlyingData()), 
        //                       min(200UL, buffer.GetLength()));
        // spdlog::info("Buffer preview (first 200 bytes):\n{}", buffer_content);
            s3Result_.insert(s3Result_.end(), payload.begin(), payload.end());
        }
    });

    // End event callback
    handler.SetEndEventCallback([&]() {
        cerr << "End of the query\n";
        hasEndEvent = true;
    });

    // Stats event callback (optional)
    handler.SetStatsEventCallback([](const Aws::S3::Model::StatsEvent &statsEvent) {
        cerr << "Bytes scanned: " << statsEvent.GetDetails().GetBytesScanned() << "\n";
        cerr << "Bytes processed: " << statsEvent.GetDetails().GetBytesProcessed() << "\n";
    });

    selectObjectContentRequest.SetEventStreamHandler(handler);

    try {
        auto selectObjectOutcome = awsClient->SelectObjectContent(selectObjectContentRequest);
        if (!selectObjectOutcome.IsSuccess()) {
            cerr << "S3 Select query failed: " << selectObjectOutcome.GetError().GetMessage() << "\n";
            return {};
        }
    } catch (const Aws::Client::AWSError<Aws::S3::S3Errors>& e) {
        cerr << "AWS SDK error: " << e.GetMessage() << "\n";
        return {};
    } catch (const std::exception& e) {
        cerr << "Unexpected error: " << e.what() << "\n";
        return {};
    }

    // Wait for the end event
    while (!hasEndEvent) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    // high_resolution_clock::time_point overallEnd = high_resolution_clock::now();
    // milliseconds overallTime = chrono::duration_cast<milliseconds>(overallEnd - begin);
    // cout<<"s3selectbyIndex耗时："<<overallTime.count()<<"ms"<<endl;
    return parseCompletePayload(s3Result_.begin(), s3Result_.end(), new_col);
}

shared_ptr<arrow::Table> s3Select(
    const string &bucket, 
    const string &key, 
    shared_ptr<Aws::S3::S3Client> awsClient,
    const vector<string> & col)
{
    high_resolution_clock::time_point begin = high_resolution_clock::now();
    const string& subject = col[0];
    const string& object = col[1];
    vector<string>new_col;
    string sql;
    if(subject.find("?") == string::npos && subject != "subject") {
        sql = "SELECT s.object FROM s3object s WHERE s.subject = '" + subject + "'";
        new_col.emplace_back(object);
    } else if(object.find("?") == string::npos && object != "object") {
        sql = "SELECT s.subject FROM s3object s WHERE s.object = '" + object + "'";
        new_col.emplace_back(subject);
    } else {
        sql = "SELECT * FROM s3object s";
        new_col.emplace_back(subject);
        new_col.emplace_back(object);
    }
    cout<<new_col.size()<<endl;
    Aws::S3::Model::SelectObjectContentRequest selectObjectContentRequest;
    selectObjectContentRequest.SetBucket(bucket);
    selectObjectContentRequest.SetKey(key);
    selectObjectContentRequest.SetExpressionType(Aws::S3::Model::ExpressionType::SQL);
    selectObjectContentRequest.SetExpression(sql);

    // Input and Output serialization
    selectObjectContentRequest.SetInputSerialization(getInputSerialization());
    selectObjectContentRequest.SetOutputSerialization(getOutputSerialization());

    // Handler to process the response
    Aws::S3::Model::SelectObjectContentHandler handler;
    vector<unsigned char> s3Result_;                         
    bool hasEndEvent = false;

    // Records event callback
    handler.SetRecordsEventCallback([&](const Aws::S3::Model::RecordsEvent &recordsEvent) {
        // cerr << "Record received\n";
        auto payload = recordsEvent.GetPayload();
        if (!payload.empty()) {
            s3Result_.insert(s3Result_.end(), payload.begin(), payload.end());
        }
    });

    // End event callback
    handler.SetEndEventCallback([&]() {
        cerr << "End of the query\n";
        hasEndEvent = true;
    });

    // Stats event callback (optional)
    handler.SetStatsEventCallback([](const Aws::S3::Model::StatsEvent &statsEvent) {
        cerr << "Bytes scanned: " << statsEvent.GetDetails().GetBytesScanned() << "\n";
        cerr << "Bytes processed: " << statsEvent.GetDetails().GetBytesProcessed() << "\n";
        cerr << "Bytes returned: " << statsEvent.GetDetails().GetBytesReturned() << "\n";
    });

    selectObjectContentRequest.SetEventStreamHandler(handler);

    try {
        auto selectObjectOutcome = awsClient->SelectObjectContent(selectObjectContentRequest);
        if (!selectObjectOutcome.IsSuccess()) {
            cerr << "S3 Select query failed: " << selectObjectOutcome.GetError().GetMessage() << "\n";
            return {};
        }
    } catch (const Aws::Client::AWSError<Aws::S3::S3Errors>& e) {
        cerr << "AWS SDK error: " << e.GetMessage() << "\n";
        return {};
    } catch (const std::exception& e) {
        cerr << "Unexpected error: " << e.what() << "\n";
        return {};
    }

    // Wait for the end event
    while (!hasEndEvent) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    high_resolution_clock::time_point overallEnd = high_resolution_clock::now();
    milliseconds overallTime = chrono::duration_cast<milliseconds>(overallEnd - begin);
    cout<<"s3select耗时："<<overallTime.count()<<"ms"<<endl;
    return parseCompletePayload(s3Result_.begin(), s3Result_.end(), new_col);
}


array<size_t, 3> getRange(const string &bucket, 
                       const string &key,
                       const string &parsed_conditions,
                       shared_ptr<Aws::S3::S3Client> awsClient)
{
    // string key_ = key + "_allindex.csv";
    // string key_ = key;
    size_t start = 0;
    size_t end = 0;
    size_t size = 0;
    string sql;

    // 如果 parsed_conditions 为空字符串或 "None"，返回默认查询
    if (parsed_conditions.empty()) {
        sql = "SELECT * FROM s3object s WHERE s.object = 'size'";
    } else {
        sql = "SELECT * FROM s3object s WHERE s.object = '" + parsed_conditions + "' OR s.object = 'size'";
    }
    cout<<sql<<endl;
    Aws::S3::Model::SelectObjectContentRequest selectObjectContentRequest;
    selectObjectContentRequest.SetBucket(bucket);
    selectObjectContentRequest.SetKey(key);
    selectObjectContentRequest.SetExpressionType(Aws::S3::Model::ExpressionType::SQL);
    selectObjectContentRequest.SetExpression(sql);

    // Input and Output serialization
    selectObjectContentRequest.SetInputSerialization(getInputSerialization());
    selectObjectContentRequest.SetOutputSerialization(getOutputSerialization());

    // Handler to process the response
    Aws::S3::Model::SelectObjectContentHandler handler;
    vector<unsigned char> s3Result_;                         
    bool hasEndEvent = false;

    // Records event callback
    handler.SetRecordsEventCallback([&](const Aws::S3::Model::RecordsEvent &recordsEvent) {
        cerr << "Record received\n";
        auto payload = recordsEvent.GetPayload();
        if (!payload.empty()) {
            s3Result_.insert(s3Result_.end(), payload.begin(), payload.end());
        }
    });

    // End event callback
    handler.SetEndEventCallback([&]() {
        cerr << "End of the query\n";
        hasEndEvent = true;
    });

    // Stats event callback (optional)
    handler.SetStatsEventCallback([](const Aws::S3::Model::StatsEvent &statsEvent) {
        cerr << "Bytes scanned: " << statsEvent.GetDetails().GetBytesScanned() << "\n";
        cerr << "Bytes processed: " << statsEvent.GetDetails().GetBytesProcessed() << "\n";
    });

    selectObjectContentRequest.SetEventStreamHandler(handler);

    try {
        auto selectObjectOutcome = awsClient->SelectObjectContent(selectObjectContentRequest);
        if (!selectObjectOutcome.IsSuccess()) {
            cerr << "S3 Select query failed: " << selectObjectOutcome.GetError().GetMessage() << "\n";
            return {};
        }
    } catch (const Aws::Client::AWSError<Aws::S3::S3Errors>& e) {
        cerr << "AWS SDK error: " << e.GetMessage() << "\n";
        return {};
    } catch (const std::exception& e) {
        cerr << "Unexpected error: " << e.what() << "\n";
        return {};
    }

    // Wait for the end event
    while (!hasEndEvent) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    try {
        string str(s3Result_.begin(), s3Result_.end());
        stringstream ss(str);
        string line;
        while (getline(ss, line)) {
            stringstream lineStream(line);
            string token;
            getline(lineStream, token, ',');
            if(token == "size") {
                getline(lineStream, token, ',');
                size = std::stoull(token); // 这里可能抛出异常
            } else {
                getline(lineStream, token, ',');
                start = std::stoull(token); // 这里可能抛出异常
                getline(lineStream, token, ',');
                end = std::stoull(token); // 这里可能抛出异常
            }
        }
    } catch (const std::invalid_argument& e) {
        cerr << "Invalid argument: " << e.what() << "\n";
        return {};
    } catch (const std::out_of_range& e) {
        cerr << "Out of range: " << e.what() << "\n";
        return {};
    } catch (const std::exception& e) {
        cerr << "Unexpected error: " << e.what() << "\n";
        return {};
    }
    return {size, start, end};
}

array<size_t, 3> getRangebyget(const string &bucket, 
                               const string &key,
                               const string &parsed_conditions,
                               shared_ptr<Aws::S3::S3Client> awsClient)
{
    size_t start = 0;
    size_t end = 0;
    size_t size = 0;
    stringstream stream;
    vector<string> fil;
    
    spdlog::info("=== getRangebyget called ===");
    spdlog::info("Bucket: {}, Key: {}, Condition: {}", bucket, key, parsed_conditions);
    
    if (!parsed_conditions.empty() && parsed_conditions != "None") {
        fil.emplace_back(parsed_conditions);
    }
    
    spdlog::info("Filter values: {}", fmt::join(fil, ", "));
    
    Aws::S3::Model::GetObjectRequest getObjectRequest;
    getObjectRequest.WithBucket(bucket).WithKey(key);

    auto getObjectOutcome = awsClient->GetObject(getObjectRequest);

    if (getObjectOutcome.IsSuccess()){
        spdlog::info("Successfully retrieved object: {}", key);
        
        Aws::IOStream& retrievedFile = getObjectOutcome.GetResult().GetBody();
        shared_ptr<arrow::io::InputStream> inputStream = make_shared<ArrowInputStream>(retrievedFile);
        
        retrievedFile.seekg(0, std::ios::beg);
        std::string header_line;
        std::string size_line;

        // 跳过第一行（表头）
        std::getline(retrievedFile, header_line);
        spdlog::info("Header: {}", header_line);

        // 读取第二行（size行）
        if (std::getline(retrievedFile, size_line)) {
            spdlog::info("Size line: {}", size_line);
    
            // 解析 size 行
            std::stringstream ss(size_line);
            std::string token;
            std::getline(ss, token, ',');
            if (token == "size") {
                std::getline(ss, token, ',');
                try {
                    size = std::stoull(token);
                   spdlog::info("Parsed size from second line: {}", size);
                } catch (const std::exception& e) {
                    spdlog::error("Failed to parse size from '{}': {}", token, e.what());
                }
            }
        }
        // 重置流，从头读取给 Arrow
        retrievedFile.clear();
        retrievedFile.seekg(0, std::ios::beg);
        
        auto read_options = arrow::csv::ReadOptions::Defaults();
        read_options.column_names = {"object", "start", "end"};
        read_options.skip_rows = 1;  // 跳过表头
        
        auto convert_options = arrow::csv::ConvertOptions::Defaults();
        convert_options.column_types["object"] = arrow::utf8();
        convert_options.column_types["start"] = arrow::utf8();
        convert_options.column_types["end"] = arrow::utf8();
        // 自动利用 Arrow 并发进行 CSV 读取
        auto csv_reader = arrow::csv::TableReader::Make(
            arrow::io::default_io_context(), inputStream,
            read_options,
            arrow::csv::ParseOptions::Defaults(),
            convert_options);
        if (!csv_reader.ok()) {
            spdlog::error("Failed to create CSV TableReader");
        }
        shared_ptr<arrow::csv::TableReader> reader = *csv_reader;

        // Read table from CSV file
        auto table = reader->Read();
        if(!table.ok()) {
           spdlog::error("转化结果为arrow表格失败: {}", table.status().ToString());
        }else {
            arrow::dataset::internal::Initialize();
            auto dataset = std::make_shared<arrow::dataset::InMemoryDataset>(table.ValueOrDie());
            auto options = std::make_shared<arrow::dataset::ScanOptions>();
            options->add_augmented_fields = false;
            options->projection = arrow::compute::project({
                // arrow::compute::field_ref("object"),
                arrow::compute::field_ref("start"),
                arrow::compute::field_ref("end")
            }, {
                // "Object",
                "Start",
                "End"
            });
            cp::Expression filter_expr = arrow::compute::literal(false);
            for (const auto& f : fil) {
                cp::Expression current_condition = arrow::compute::equal(
                arrow::compute::field_ref("object"),
                arrow::compute::literal(f)
                ); 
                // 将当前条件与已有条件进行 "or" 组合
                filter_expr = arrow::compute::or_(filter_expr, current_condition);
            }
            // 输出最终的过滤表达式
            std::cout << "Final filter expression: " << filter_expr.ToString() << std::endl;
            options->filter = filter_expr;

            auto scan_node_options = arrow::dataset::ScanNodeOptions{dataset, options};
            arrow::acero::Declaration scan{"scan", std::move(scan_node_options)};
            spdlog::info("scan declaration.");

            ac::Declaration filter{
              "filter", {std::move(scan)}, ac::FilterNodeOptions(std::move(filter_expr))};
            arrow::Result<std::shared_ptr<arrow::Table>> status = arrow::acero::DeclarationToTable(std::move(filter));
            // 检查是否成功
            if (!status.ok()) {
                spdlog::error("Error during filter: {}", status.status().ToString());
            }
            std::shared_ptr<arrow::Table> response_table = status.ValueOrDie();
            spdlog::info("Number of rows: {}", response_table->num_rows());

            cout<<response_table->schema()->ToString()<<endl;
            cout<<response_table->num_columns()<<endl;
            //  遍历每一行
            for (int row = 0; row < response_table->num_rows(); ++row) {
                for (int col = 0; col < 3; ++col) {
                    auto column = response_table->column(col);
                    int64_t row_in_chunk = row;
                    for (int64_t chunk_index = 0; chunk_index < column->num_chunks(); ++chunk_index) {
                        auto chunked_array = column->chunk(chunk_index);

                        // 检查 row_in_chunk 是否在当前 chunk 的范围内
                        if (row_in_chunk < chunked_array->length()) {
                            // 获取标量值
                            auto result = chunked_array->GetScalar(row_in_chunk);
                            if (result.ok()) {
                                auto scalar = result.ValueOrDie()->ToString();
                                stream << scalar;
                                stream << ",";
                            }
                        }
                    }
                }
                stream << '\n';
            }
            cout<<stream.str()<<endl;
        }
        try {
            string line;
            while (getline(stream, line)) {
                stringstream lineStream(line);
                string token;
                getline(lineStream, token, ',');
                if(token == "size") {
                    getline(lineStream, token, ',');
                    size = std::stoull(token); // 这里可能抛出异常
                } else {
                    getline(lineStream, token, ',');
                    start = std::stoull(token); // 这里可能抛出异常
                    getline(lineStream, token, ',');
                    end = std::stoull(token); // 这里可能抛出异常
                }
            }
        } catch (const std::invalid_argument& e) {
            cerr << "Invalid argument: " << e.what() << "\n";
            return {};
        } catch (const std::out_of_range& e) {
            cerr << "Out of range: " << e.what() << "\n";
            return {};
        } catch (const std::exception& e) {
            cerr << "Unexpected error: " << e.what() << "\n";
            return {};
        }
    }else {
           std::cerr << "获取对象失败: " << getObjectOutcome.GetError().GetMessage() << std::endl;
    }                  

    return {size, start, end};
}

shared_ptr<arrow::Table> getobjectfilter(shared_ptr<arrow::Table> table, 
                                string col1,
                                string col2,
                                vector<size_t> fil)
{
    arrow::dataset::internal::Initialize();
    auto dataset = std::make_shared<arrow::dataset::InMemoryDataset>(table);
    auto options = std::make_shared<arrow::dataset::ScanOptions>();
    cp::Expression filter_expr = arrow::compute::literal(false);
    for (const auto& f : fil) {
        cp::Expression current_condition = arrow::compute::equal(
            arrow::compute::field_ref(col1),
            arrow::compute::literal(f)
        ); 
        // 将当前条件与已有条件进行 "or" 组合
        filter_expr = arrow::compute::or_(filter_expr, current_condition);
    }
    // 输出最终的过滤表达式
    std::cout << "Final filter expression: " << filter_expr.ToString() << std::endl;
    options->filter = filter_expr;
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
    // cout<<response_table->num_columns()<<endl;
    return response_table;
}
