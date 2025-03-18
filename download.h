#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <iostream>
#include <mutex>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <csignal>
#include <atomic>
#include <thread>
#include <windows.h>

#define THREAD_NUM 10

using namespace std;

// 文件下载信息结构体
struct FileInfo {
    const char* url;          // 下载URL
    char* fileptr;            // 文件内存映射指针
    int offset;               // 当前下载偏移量
    int end;                  // 下载结束位置
    atomic<double> download;  // 当前下载量
    double totalDownload;     // 总下载量
    ifstream* recordFile;     // 记录文件流
};

extern struct FileInfo** pInfoTable; // 全局文件信息表
extern atomic<long> downloadFileLength;     // 文件总长度
extern mutex printMutex;                       // 打印互斥锁

// 写入回调函数
size_t writeFunc(void* ptr, size_t size, size_t memb, void* userdata);

// 进度回调函数
int progressFunc(void* userdata, double totalDownload, double nowDownload, double, double);

// 获取文件长度
long getDownloadFileLength(const char* url);

// 工作线程函数
unsigned int __stdcall worker(void* arg);

// 下载函数
int download(const char* url, const char* filename);

// 信号处理函数
void signalHandler(int signum);

#endif // DOWNLOAD_H