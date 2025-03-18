#include <iostream>
#include <mutex>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <csignal>

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

struct FileInfo** pInfoTable; // 全局文件信息表
atomic<long> downloadFileLength(0);     // 文件总长度
mutex printMutex;                       // 打印互斥锁

// 写入回调函数
size_t writeFunc(void* ptr, size_t size, size_t memb, void* userdata) {
    FileInfo* info = static_cast<FileInfo*>(userdata);
    memcpy(info->fileptr + info->offset, ptr, size * memb);
    info->offset += size * memb;
    return size * memb;
}

// 进度回调函数
int progressFunc(void* userdata, double totalDownload, double nowDownload, double, double) {
    FileInfo* info = static_cast<FileInfo*>(userdata);
    info->download = nowDownload;
    info->totalDownload = totalDownload;

    if (totalDownload > 0) {
        double allDownload = 0;
        double total = 0;

        for (int i = 0; i <= THREAD_NUM; i++)
        {
            allDownload += pInfoTable[i]->download;
            total += pInfoTable[i]->totalDownload;
        }

        int percent = static_cast<int>(allDownload / total * 100);
        static int lastPercent = 0;

        lock_guard<mutex> lock(printMutex);
        if (percent > lastPercent) {
            cout << "Progress: " << percent << "%" << endl;
            lastPercent = percent;
        }
    }

    return 0;
}

// 获取文件长度
long getDownloadFileLength(const char* url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "Failed to initialize CURL." << endl;
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HEADER, 1);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "CURL error: " << curl_easy_strerror(res) << endl;
        curl_easy_cleanup(curl);
        return -1;
    }

    double length;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length);
    curl_easy_cleanup(curl);

    return static_cast<long>(length);
}

// 工作线程函数
unsigned int __stdcall worker(void* arg) {
    struct FileInfo* info = (struct FileInfo*)arg;
    char range[64] = { 0 };
    
    // 从存档中读取待下载的范围
    if (info->recordFile && *info->recordFile) {
        string line;
        if (getline(*info->recordFile, line)) {
            istringstream iss(line);
            char dash;
            if (!(iss >> info->offset >> dash >> info->end)) {
                cerr << "Failed to parse line: " << line << endl;
                info->offset = 0;
                info->end = 0;
            }
        }
    }

    if (info->offset > info->end) return 0;

    // 拼接字符串表示下载范围
    ostringstream oss;
    oss << info->offset << "-" << info->end;
    strcpy_s(range, oss.str().c_str());
    //snprintf(range, sizeof(range), "%d-%d", info->offset, info->end);

    {
        // 加锁确保打印格式正确
        lock_guard<mutex> lock(printMutex);
        cout << "Thread ID: " << this_thread::get_id() << ", downloading range: " << range << endl;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "Failed to initialize CURL." << endl;
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, info->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, info);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressFunc);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, info);
    curl_easy_setopt(curl, CURLOPT_RANGE, range);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "CURL error: " << curl_easy_strerror(res) << endl;
    }

    curl_easy_cleanup(curl);
}

// 下载函数
int download(const char* url, const char* filename) {
    // 获取文件长度
    long fileLength = getDownloadFileLength(url);
    if (fileLength <= 0) {
        cerr << "Invalid file length." << endl;
        return -1;
    }
    cout << "File length: " << fileLength << " bytes" << endl;

    // 创建或打开文件
    HANDLE hFile = CreateFileA(
        filename,
        GENERIC_READ | GENERIC_WRITE, // 读写权限
        0,
        NULL,
        OPEN_ALWAYS, // 如果文件不存在则创建
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        cerr << "Failed to open file: " << filename << endl;
        return -1;
    }

    // 设置文件大小
    LARGE_INTEGER fileSize;
    fileSize.QuadPart = fileLength;
    if (!SetFilePointerEx(hFile, fileSize, NULL, FILE_BEGIN)) {
        cerr << "Failed to set file pointer." << endl;
        CloseHandle(hFile);
        return -1;
    }
    if (!SetEndOfFile(hFile)) {
        cerr << "Failed to set end of file." << endl;
        CloseHandle(hFile);
        return -1;
    }

    // 创建文件映射对象
    HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, 0, fileLength, NULL);
    if (hMapping == NULL) {
        cerr << "Failed to create file mapping." << endl;
        CloseHandle(hFile);
        return -1;
    }

    // 将文件映射到内存
    char* fileptr = (char*)MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, fileLength);
    if (fileptr == NULL) {
        cerr << "Failed to map view of file." << endl;
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return -1;
    }

    // 打开记录文件（用于断点续传）
    ifstream inFile("record.txt", ios::in);

    // 初始化线程下载信息
    int i = 0;
    long partSize = fileLength / THREAD_NUM;
    struct FileInfo* info[THREAD_NUM + 1] = { NULL };
    for (i = 0; i <= THREAD_NUM; i++) {
        info[i] = (struct FileInfo*)malloc(sizeof(struct FileInfo));
        memset(info[i], 0, sizeof(struct FileInfo));

        info[i]->offset = i * partSize;
        info[i]->end = (i < THREAD_NUM) ? (i + 1) * partSize - 1 : fileLength - 1;
        info[i]->fileptr = fileptr;
        info[i]->url = url;
        info[i]->download = 0;
        info[i]->totalDownload = 0;
        info[i]->recordFile = &inFile;
    }
    pInfoTable = info;

    // 创建线程并开始下载
    HANDLE hThreads[THREAD_NUM + 1] = { NULL };
    for (i = 0; i <= THREAD_NUM; i++) {
        hThreads[i] = (HANDLE)_beginthreadex(NULL, 0, worker, info[i], 0, NULL);
        if (hThreads[i] == NULL) {
            cerr << "Failed to create thread." << endl;
            return -1;
        }
        Sleep(1); // 短暂休眠，确保线程按顺序启动
    }

    // 等待所有线程完成
    for (i = 0; i <= THREAD_NUM; i++) {
        WaitForSingleObject(hThreads[i], INFINITE);
        CloseHandle(hThreads[i]);
    }

    // 释放资源
    for (i = 0; i <= THREAD_NUM; i++) {
        free(info[i]);
    }

    // 清理文件映射和句柄
    UnmapViewOfFile(fileptr);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    return 0;
}

// 信号处理函数
void signalHandler(int signum) {
    //cout << "Interrupt signal (" << signum << ") received." << endl;
    cout << "Download stopped." << endl;

    ofstream outFile("record.txt", ios::out | ios::trunc);
    if (!outFile) {
        cerr << "Failed to open record file." << endl;
        exit(1);
    }

    // 写入断点信息
    for (int i = 0; i <= THREAD_NUM; i++) {
        if (pInfoTable[i]) {
            outFile << pInfoTable[i]->offset << "-" << pInfoTable[i]->end << "\r\n";
        }
    }

    outFile.close();
    exit(signum);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <URL> <output file>" << endl;
        return -1;
    }

    signal(SIGINT, signalHandler);

    return download(argv[1], argv[2]);
}