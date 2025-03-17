#include <iostream>
#include <windows.h>
#include <curl/curl.h>
#include <sstream>
#include <cstring>
#include <process.h>
#include <mutex>

#define THREAD_NUM 10

using namespace std;

struct fileInfo {
	const char* url;
	char* fileptr;
	int offset;
	int end;
	DWORD threadId;
	double download;
};

struct fileInfo** pInfoTable;
double downloadFileLength = 0;
mutex printMutex;

size_t writeFunc(void* ptr, size_t size, size_t memb, void* userdata) {
	struct fileInfo* info = (struct fileInfo*)userdata;
	//printf("writeFunc: %d\n", size * memb);

	memcpy(info->fileptr + info->offset, ptr, size * memb);
	info->offset += size * memb;

	return size * memb;
}

int progressFunc(void* userdata, double totalDownload, double nowDownload, double totalUpload, double nowUpload) {
	int percent = 0;
	static int print = 1;
	struct fileInfo* info = (struct fileInfo*)userdata;
	info->download = nowDownload;

	if (totalDownload > 0)
	{
		int i = 0;
		double allDownload = 0;
		for (i = 0; i <= THREAD_NUM; i++)
		{
			allDownload += pInfoTable[i]->download;
		}

		percent = (int)(allDownload / downloadFileLength * 100);
	}

	lock_guard<mutex> lock(printMutex);
	if (percent == print)
	{
		printf("percent: %d%%\n", percent);
		print++;
	}

	return 0;
}

long getDownloadFileLength(const char* url) {
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

unsigned int __stdcall worker(void* arg) {
	struct fileInfo* info = (struct fileInfo*)arg;

	char range[64] = { 0 };
	ostringstream oss;
	oss << info->offset << "-" << info->end;
	strcpy_s(range, oss.str().c_str());

	// 获取并存储线程 ID
	info->threadId = GetCurrentThreadId();
	printf("threadId: %lu, download from: %d to: %d\n", info->threadId, info->offset, info->end);

	// 初始化 CURL
	CURL* curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, info->url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunc);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, info);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressFunc);
	curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, info);
	curl_easy_setopt(curl, CURLOPT_RANGE, range);
	curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem"); // 指定证书文件路径

	// 执行下载
	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		cerr << "CURL error: " << res << endl;
	}

	// 清理资源
	curl_easy_cleanup(curl);

	return 0;
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

	int i = 0;
	long partSize = fileLength / THREAD_NUM;
	struct fileInfo* info[THREAD_NUM + 1] = { NULL };
	for (i = 0; i <= THREAD_NUM; i++)
	{
		info[i] = (struct fileInfo*)malloc(sizeof(struct fileInfo));

		info[i]->offset = i * partSize;
		if (i < THREAD_NUM)
		{
			info[i]->end = (i + 1) * partSize - 1;
		}
		else
		{
			info[i]->end = fileLength - 1;
		}
		info[i]->fileptr = fileptr;
		info[i]->url = url;
		info[i]->download = 0;
	}
	pInfoTable = info;

	HANDLE hThreads[THREAD_NUM + 1] = { NULL };
	for (i = 0; i <= THREAD_NUM; i++) {
		hThreads[i] = (HANDLE)_beginthreadex(NULL, 0, worker, info[i], 0, NULL);
		if (hThreads[i] == NULL) {
			cerr << "Failed to create thread." << endl;
			return -1;
		}
	}

	for (i = 0; i <= THREAD_NUM; i++) {
		WaitForSingleObject(hThreads[i], INFINITE);
		CloseHandle(hThreads[i]);
	}

	for (i = 0; i <= THREAD_NUM; i++) {
		free(info[i]);
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
}