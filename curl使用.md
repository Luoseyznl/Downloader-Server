### libcurl - 客户端 URL 传输库

- 支持多种协议（HTTP/HTTPS/FTP/SMTP/POP3...）
- 可配置超时、重定向、请求头、认证、Cookie、代理等
- 可用于同步与异步传输（支持 multi interface）
- 支持断点续传、进度回调、文件上传/下载等功能

#### 1. easy interface

1. 初始化 `CURL *curl = curl_easy_init();` 
   - curl_easy_* 是线程安全的，只要每个线程使用自己的 CURL*
2. 设置参数
   - `curl_easy_setopt(curl, CURLOPT_URL, "http://example.com");`
   - 常见配置选项

        | 设置项                                               | 含义            |
        | ------------------------------------------------- | ------------- |
        | `CURLOPT_URL`                                     | 设置请求 URL      |
        | `CURLOPT_FOLLOWLOCATION`                          | 自动跟随重定向       |
        | `CURLOPT_TIMEOUT`                                 | 设置超时时间（秒）     |
        | `CURLOPT_USERAGENT`                               | 设置 User-Agent |
        | `CURLOPT_POSTFIELDS`                              | 设置 POST 数据    |
        | `CURLOPT_HTTPHEADER`                              | 添加自定义 HTTP 头  |
        | `CURLOPT_WRITEDATA` / `CURLOPT_WRITEFUNCTION`     | 设置写入回调        |
        | `CURLOPT_RESUME_FROM`                             | 断点续传开始位置      |
        | `CURLOPT_NOPROGRESS` + `CURLOPT_XFERINFOFUNCTION` | 进度回调          |
   - 高级功能
     1. 断点续传 `curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, <file_offset>);`
     2. 写入文件
        ```
        FILE* fp = fopen("file.zip", "wb");
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        ```
     3. 并发传输
        使用 curl_multi_* 接口支持并发下载（推荐用于大规模并发任务）


3. 执行请求
   - `CURLcode res = curl_easy_perform(curl);`
4. 清理资源
   - `curl_easy_cleanup(curl);`
