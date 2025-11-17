#include "query.h" 
#include <alibabacloud/oss/model/InputFormat.h>
#include <alibabacloud/oss/model/OutputFormat.h>

AlibabaCloud::OSS::CSVInputFormat getCSVInputFormatOSS() {
    using namespace AlibabaCloud::OSS;

    CSVInputFormat format;

    format.setHeaderInfo(CSVHeader::Use);         // 与 AWS CSV Header = USE 一致
    format.setRecordDelimiter("\n");              // 行分隔符
    format.setFieldDelimiter(",");                // 字段分隔符
    format.setQuoteChar("\"");                    // 引号
    format.setCommentChar("#");                   // 注释符

    format.setCompressionType(CompressionType::NONE);

    return format;
}

AlibabaCloud::OSS::CSVOutputFormat getCSVOutputFormatOSS() {
    using namespace AlibabaCloud::OSS;

    CSVOutputFormat output;

    output.setRecordDelimiter("\n");
    output.setFieldDelimiter(",");

    // 这四个开关 AWS CSV 默认都是 false（RAW data OFF, no CRC, no extra header）
    output.setKeepAllColumns(true);      // 通常 ON，保持所有列
    output.setOutputRawData(false);
    output.setEnablePayloadCrc(false);
    output.setOutputHeader(false);

    return output;
}

// Aws::S3::Model::InputSerialization getInputSerialization() {
//     Aws::S3::Model::InputSerialization inputSerialization;
//     Aws::S3::Model::CSVInput csvInput;

//     csvInput.SetFileHeaderInfo(Aws::S3::Model::FileHeaderInfo::USE);
//     csvInput.SetFieldDelimiter(",");
//     csvInput.SetRecordDelimiter("\n");
    
//     inputSerialization.SetCSV(csvInput);
//     return inputSerialization;
// }

// Aws::S3::Model::OutputSerialization getOutputSerialization() {
//     Aws::S3::Model::OutputSerialization outputSerialization;
//     Aws::S3::Model::CSVOutput csvOutput;

//     csvOutput.SetFieldDelimiter(",");
//     csvOutput.SetRecordDelimiter("\n");
    
//     outputSerialization.SetCSV(csvOutput);
//     return outputSerialization;
// }

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

// shared_ptr<arrow::Table> parseCompletePayload (const Aws::Vector<unsigned char>::iterator &from,
// 	                                           const Aws::Vector<unsigned char>::iterator &to,
//                                                const vector<string> & col
//                                                ) 
// {
//     auto parse_options = arrow::csv::ParseOptions::Defaults();
//     auto read_options = arrow::csv::ReadOptions::Defaults();
//     read_options.column_names = col; //设置列名
//     auto convert_options = arrow::csv::ConvertOptions::Defaults();
//     for (const auto& column_name : col) {
//         convert_options.column_types[column_name] = arrow::int32();  // 将列设置为 int32 类型
//     }
//     // if(col.size() == 2){
//         // read_options.skip_rows = 1; // 跳过表头
//     // }
//     // Create a reader
//     arrow::io::IOContext ioContext;
//     auto reader = std::make_shared<arrow::io::BufferReader>(from.base(), std::distance(from, to));
//     auto createResult = arrow::io::BufferedInputStream::Create(CSV_READER_BUFFER_SIZE,
// 															 arrow::default_memory_pool(),
// 															 reader,
// 															 -1);
//     if (!createResult.ok()){
//         spdlog::error("Cannot parse S3 Select payload  |  Could not create a reader, error: '{}'", createResult.status().message());
//         return nullptr;
//     }
//     auto input = *createResult;

//     // Instantiate TableReader from input stream and options
//     auto makeReaderResult = arrow::csv::TableReader::Make(ioContext,
// 													  	  input,
// 													  	  read_options,
// 														  parse_options,
// 														  convert_options);
//     if (!makeReaderResult.ok()){
//         spdlog::error("Cannot parse S3 Select payload  |  Could not create a table reader, error: '{}'",
// 		  makeReaderResult.status().message());
//         return nullptr;
//     }
//     auto tableReader = *makeReaderResult;
//     auto table = tableReader->Read();
//     if(table.ok()){
//         // cout<<table.ValueOrDie()->schema()->ToString()<<endl;
//         // cout<<table.ValueOrDie()->num_rows()<<endl;
//         return table.ValueOrDie();
//     } else {
//         spdlog::error("读取失败！错误信息: {}", table.status().message());
//         return nullptr;
//     }
// }

// class MyUnderlyingStream : public Aws::IOStream
// {
//     public:
//         using Base = Aws::IOStream;
//         // Provide a customer-controlled streambuf to hold data from the bucket.
//         explicit MyUnderlyingStream(std::streambuf* buf)
//             : Base(buf)
//         {}

//         ~MyUnderlyingStream() override = default;
// };


shared_ptr<arrow::Table> getObject(
    const string &bucket, 
    const string &key, 
    shared_ptr<OssClient> ossclient,
    const vector<string> & col,
    size_t length)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    // 创建阿里云 OSS GetObject 请求
    GetObjectRequest request(bucket, key);

    auto outcome = ossclient->GetObject(request);

    if (!outcome.isSuccess()) {
        spdlog::error("File download failed: {}, Message: {}", 
                     outcome.error().Code(), outcome.error().Message());
        return nullptr;
    } else {
        // 获取响应内容
        std::shared_ptr<std::iostream> content = outcome.result().Content();
        
        shared_ptr<arrow::io::InputStream> inputStream = make_shared<ArrowInputStream>(*content);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        spdlog::info("转换为 Arrow 的输入流用时 {} ms", duration.count());

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
            return table.ValueOrDie();
        } else {
            spdlog::error("转化结果为arrow表格失败: {}", table.status().ToString());
            return nullptr;
        }
    }
} 

shared_ptr<arrow::Table> getObjectbyIndex(
    const string &bucket, 
    const string &key, 
    shared_ptr<OssClient> ossclient,
    const vector<string> & col,
    size_t start,
    size_t end)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    // 创建阿里云 OSS GetObject 请求
    GetObjectRequest getObjectRequest(bucket, key);
    getObjectRequest.setRange(start,end-1);
    // 发送请求
    auto getObjectOutcome = ossclient->GetObject(getObjectRequest);

    if (getObjectOutcome.isSuccess()){
        // 获取响应流
        std::shared_ptr<std::iostream> content = getObjectOutcome.result().Content();
        shared_ptr<arrow::io::InputStream> inputStream = make_shared<ArrowInputStream>(*content);
    
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
    shared_ptr<OssClient> ossclient,
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
        sql = "SELECT object FROM OSSOBJECT WHERE subject = '" + subject + "'";
        new_col.emplace_back(object);
    } else if(object.find("?") == string::npos && object != "object") {
        sql = "SELECT subject FROM OSSOBJECT WHERE object = '" + object + "'";
        new_col.emplace_back(subject);
    } else {
        sql = "SELECT * FROM OSSOBJECT";
        new_col.emplace_back(subject);
        new_col.emplace_back(object);
    }

    AlibabaCloud::OSS::SelectObjectRequest request(bucket, key);
    request.setExpression(sql,AlibabaCloud::OSS::ExpressionType::SQL);
    request.setRange(start, end);

    // 输入格式（CSV）
    CSVInputFormat input = getCSVInputFormatOSS();
    request.setInputFormat(input);

    // 输出格式（CSV）
    CSVOutputFormat output = getCSVOutputFormatOSS();
    request.setOutputFormat(output);

    auto outcome = ossclient->SelectObject(request);
    if (!outcome.isSuccess()) {
        cerr << "OSS SelectObject failed: " << outcome.error().Message() << endl;
        return nullptr;
    }else {
        std::shared_ptr<std::iostream> content = outcome.result().Content();
        shared_ptr<arrow::io::InputStream> inputStream = make_shared<ArrowInputStream>(*content);
    
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - begin);
        spdlog::info("转换为 Arrow 的输入流用时 {} ms", duration.count());

        // 读取 CSV 文件
        auto read_options = arrow::csv::ReadOptions::Defaults();
        read_options.column_names = col; //设置列名

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
    // high_resolution_clock::time_point overallEnd = high_resolution_clock::now();
    // milliseconds overallTime = chrono::duration_cast<milliseconds>(overallEnd - begin);
    // cout<<"s3selectbyIndex耗时："<<overallTime.count()<<"ms"<<endl;
}

shared_ptr<arrow::Table> s3Select(
    const string &bucket, 
    const string &key, 
    shared_ptr<OssClient> ossclient,
    const vector<string> & col)
{
    high_resolution_clock::time_point begin = high_resolution_clock::now();
    const string& subject = col[0];
    const string& object = col[1];
    vector<string>new_col;
    string sql;
    if(subject.find("?") == string::npos && subject != "subject") {
        sql = "SELECT object FROM OSSOBJECT WHERE subject = '" + subject + "'";
        new_col.emplace_back(object);
    } else if(object.find("?") == string::npos && object != "object") {
        sql = "SELECT subject FROM OSSOBJECT WHERE object = '" + object + "'";
        new_col.emplace_back(subject);
    } else {
        sql = "SELECT * FROM OSSOBJECT";
        new_col.emplace_back(subject);
        new_col.emplace_back(object);
    }
    cout<<object<<endl;

    AlibabaCloud::OSS::SelectObjectRequest request(bucket, key);
    request.setExpression(sql,AlibabaCloud::OSS::ExpressionType::SQL);

    // 输入格式（CSV）
    CSVInputFormat input = getCSVInputFormatOSS();
    request.setInputFormat(input);

    // 输出格式（CSV）
    CSVOutputFormat output = getCSVOutputFormatOSS();
    request.setOutputFormat(output);

    auto outcome = ossclient->SelectObject(request);
    if (!outcome.isSuccess()) {
        cerr << "OSS SelectObject failed: " << outcome.error().Message() << endl;
        return nullptr;
    }else {
        std::shared_ptr<std::iostream> content = outcome.result().Content();
        shared_ptr<arrow::io::InputStream> inputStream = make_shared<ArrowInputStream>(*content);
    
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - begin);
        spdlog::info("转换为 Arrow 的输入流用时 {} ms", duration.count());

        // 读取 CSV 文件
        auto read_options = arrow::csv::ReadOptions::Defaults();
        read_options.column_names = col; //设置列名

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

    // high_resolution_clock::time_point overallEnd = high_resolution_clock::now();
    // milliseconds overallTime = chrono::duration_cast<milliseconds>(overallEnd - begin);
    // cout<<"s3select耗时："<<overallTime.count()<<"ms"<<endl;
}


// array<size_t, 3> getRange(const string &bucket, 
//                        const string &key,
//                        const string &parsed_conditions,
//                        shared_ptr<Aws::S3::S3Client> ossclient)
// {
//     // string key_ = key + "_index.csv";
//     // string key_ = key;
//     size_t start = 0;
//     size_t end = 0;
//     size_t size = 0;
//     string sql;

//     // 如果 parsed_conditions 为空字符串或 "None"，返回默认查询
//     if (parsed_conditions.empty()) {
//         sql = "SELECT * FROM s3object s WHERE s.object = 'size'";
//     } else {
//         sql = "SELECT * FROM s3object s WHERE s.object = '" + parsed_conditions + "' OR s.object = 'size'";
//     }
//     cout<<sql<<endl;
//     Aws::S3::Model::SelectObjectContentRequest selectObjectContentRequest;
//     selectObjectContentRequest.SetBucket(bucket);
//     selectObjectContentRequest.SetKey(key);
//     selectObjectContentRequest.SetExpressionType(Aws::S3::Model::ExpressionType::SQL);
//     selectObjectContentRequest.SetExpression(sql);

//     // Input and Output serialization
//     selectObjectContentRequest.SetInputSerialization(getInputSerialization());
//     selectObjectContentRequest.SetOutputSerialization(getOutputSerialization());

//     // Handler to process the response
//     Aws::S3::Model::SelectObjectContentHandler handler;
//     vector<unsigned char> s3Result_;                         
//     bool hasEndEvent = false;

//     // Records event callback
//     handler.SetRecordsEventCallback([&](const Aws::S3::Model::RecordsEvent &recordsEvent) {
//         cerr << "Record received\n";
//         auto payload = recordsEvent.GetPayload();
//         if (!payload.empty()) {
//             s3Result_.insert(s3Result_.end(), payload.begin(), payload.end());
//         }
//     });

//     // End event callback
//     handler.SetEndEventCallback([&]() {
//         cerr << "End of the query\n";
//         hasEndEvent = true;
//     });

//     // Stats event callback (optional)
//     handler.SetStatsEventCallback([](const Aws::S3::Model::StatsEvent &statsEvent) {
//         cerr << "Bytes scanned: " << statsEvent.GetDetails().GetBytesScanned() << "\n";
//         cerr << "Bytes processed: " << statsEvent.GetDetails().GetBytesProcessed() << "\n";
//     });

//     selectObjectContentRequest.SetEventStreamHandler(handler);

//     try {
//         auto selectObjectOutcome = ossclient->SelectObjectContent(selectObjectContentRequest);
//         if (!selectObjectOutcome.IsSuccess()) {
//             cerr << "S3 Select query failed: " << selectObjectOutcome.GetError().GetMessage() << "\n";
//             return {};
//         }
//     } catch (const Aws::Client::AWSError<Aws::S3::S3Errors>& e) {
//         cerr << "AWS SDK error: " << e.GetMessage() << "\n";
//         return {};
//     } catch (const std::exception& e) {
//         cerr << "Unexpected error: " << e.what() << "\n";
//         return {};
//     }

//     // Wait for the end event
//     while (!hasEndEvent) {
//         this_thread::sleep_for(chrono::milliseconds(100));
//     }

//     try {
//         string str(s3Result_.begin(), s3Result_.end());
//         stringstream ss(str);
//         string line;
//         while (getline(ss, line)) {
//             stringstream lineStream(line);
//             string token;
//             getline(lineStream, token, ',');
//             if(token == "size") {
//                 getline(lineStream, token, ',');
//                 size = std::stoull(token); // 这里可能抛出异常
//             } else {
//                 getline(lineStream, token, ',');
//                 start = std::stoull(token); // 这里可能抛出异常
//                 getline(lineStream, token, ',');
//                 end = std::stoull(token); // 这里可能抛出异常
//             }
//         }
//     } catch (const std::invalid_argument& e) {
//         cerr << "Invalid argument: " << e.what() << "\n";
//         return {};
//     } catch (const std::out_of_range& e) {
//         cerr << "Out of range: " << e.what() << "\n";
//         return {};
//     } catch (const std::exception& e) {
//         cerr << "Unexpected error: " << e.what() << "\n";
//         return {};
//     }

//     return {size, start, end};
// }

array<size_t, 3> getRangebyget(const string &bucket, 
                       const string &key,
                       const string &parsed_conditions,
                       shared_ptr<OssClient> ossclient)
{
    // string key_ = key + "_index.csv";
    size_t start = 0;
    size_t end = 0;
    size_t size = 0;
    stringstream stream;
    vector<string> fil;
    fil.emplace_back("size");

    // 创建阿里云 OSS GetObject 请求
    GetObjectRequest getObjectRequest(bucket, key);

    // 如果 parsed_conditions 为空字符串或 "None"，返回默认查询
    if (!parsed_conditions.empty()) {
        fil.emplace_back(parsed_conditions);
    }
    
    // 发送请求
    auto getObjectOutcome = ossclient->GetObject(getObjectRequest);

    if (getObjectOutcome.isSuccess()){
        // 获取响应流
        std::shared_ptr<std::iostream> content = getObjectOutcome.result().Content();
        shared_ptr<arrow::io::InputStream> inputStream = make_shared<ArrowInputStream>(*content);
        auto read_options = arrow::csv::ReadOptions::Defaults();
        read_options.column_names = {"object","start","end"}; //设置列名
        read_options.skip_rows = 1; // 跳过表头
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
           std::cerr << "获取对象失败: " << getObjectOutcome.error().Code() 
                  << ", Message: " << getObjectOutcome.error().Message() << std::endl;
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
