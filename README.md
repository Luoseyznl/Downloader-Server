# DownloaderApp

这是一个基于 C++17 的并发文件下载工具，支持命令行参数控制并发数。适合用于大文件的分片并发下载。

## 特性

- **多线程分片下载**：基于 Intel TBB（Threading Building Blocks）实现高效并发分片下载。
- **支持 HTTP/HTTPS**：使用 libcurl 实现 HTTP/HTTPS 文件下载，支持服务器 Range 请求。
- **灵活的并发控制**：可通过命令行参数 `--download_threads=N` 控制下载线程数。
- **自动日志记录**：内置线程安全日志系统，支持日志轮转，日志文件保存在 `logs/` 目录。
- **易于扩展**：代码结构清晰，便于集成更多协议（如 FTP、SFTP）或自定义下载逻辑。

---

## 使用方法

### 编译

确保已安装依赖：`libtbb-dev`、`libcurl4-openssl-dev`、`libgflags-dev`

```sh
mkdir build && cd build
cmake ..
make -j
```

### 命令行用法

```sh
./DownloaderApp <url> <output_path> [--download_threads=N]
```

- `<url>`：要下载的文件的 HTTP/HTTPS 直链
- `<output_path>`：保存文件的路径（文件名）
- `--download_threads=N`：可选，指定并发线程数（默认最大线程数）

**示例：**

```sh
./DownloaderApp https://example.com/bigfile.zip bigfile.zip --download_threads=8
```

### 日志

- 日志文件保存在 `logs/downloader.log`
- 可用 `tail -f logs/downloader.log` 实时查看


