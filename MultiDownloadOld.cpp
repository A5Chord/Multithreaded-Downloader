#include <iostream>
#include <windows.h>
#include <pthread.h>
#include <curl/curl.h>

#define THREAD_COUNT 10

using namespace std;

struct fileInfo {
    char* fileptr;
    int offset;
};

size_t writeFunc(void* ptr, size_t size, size_t memb, void* userdata) {
    struct fileInfo* info = (struct fileInfo*)userdata;
    cout << "writeFunc: " << size * memb << endl;

    memcpy(info->fileptr + info->offset, ptr, size * memb);
    info->offset += size * memb;

    return size * memb;
}

long getDownloadFileLength(const char* url) {
    double downloadFileLength = 0;
    CURL* curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HEADER, 1);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem"); // 指定证书文件路径

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &downloadFileLength);
    }
    else {
        downloadFileLength = -1;
        cerr << "download error" << endl;
    }
    curl_easy_cleanup(curl);

    return static_cast<long>(downloadFileLength); // 转换为 long
}

void* worker(void* arg) {
    char* fileptr = arg;

    // 初始化 fileInfo 结构
    struct fileInfo* info = (struct fileInfo*)malloc(sizeof(struct fileInfo));
    if (info == NULL) {
        cerr << "Failed to allocate memory for fileInfo." << endl;
        UnmapViewOfFile(fileptr);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return -1;
    }
    info->fileptr = fileptr;
    info->offset = 0;

    // 初始化 CURL
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, info);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem"); // 指定证书文件路径

    // 执行下载
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "CURL error: " << res << endl;
    }

    // 清理资源
    curl_easy_cleanup(curl);
    free(info);
}

int download(const char* url, const char* filename) {
    long fileLength = getDownloadFileLength(url);
    cout << "downloadFileLength: " << fileLength << endl;

    // 打开文件
    HANDLE hFile = CreateFileA(
        filename,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
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
    if (!SetFilePointerEx(hFile, fileSize, NULL, FILE_BEGIN)){
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
    HANDLE hMapping = CreateFileMappingA(
        hFile,
        NULL,
        PAGE_READWRITE,
        0,
        fileLength,
        NULL
    );
    if (hMapping == NULL) {
        cerr << "Failed to create file mapping." << endl;
        CloseHandle(hFile);
        return -1;
    }

    // 将文件映射到内存
    char* fileptr = (char*)MapViewOfFile(
        hMapping,
        FILE_MAP_WRITE,
        0,
        0,
        fileLength
    );
    if (fileptr == NULL) {
        cerr << "Failed to map view of file." << endl;
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return -1;
    }

    int i = 0;
    pthread_t threadId[THREAD_COUNT] = { 0 };
    for (i = 0; i < THREAD_COUNT; i++)
    {
        pthread_create(&threadId[i], NULL, worker, fileptr);
    }

    for (i = 0; i < THREAD_COUNT; i++)
    {
        pthread_join(threadId[i], NULL);
    }

    UnmapViewOfFile(fileptr);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "arg error! " << endl;
        return -1;
    }

    return download(argv[1], argv[2]);
    //return download("https://down.clashcn.com/soft/clashcn.com_Clash.for.Windows-0.20.39-win-CN.7z", "clash");
}