#include <iostream>
#include <mutex>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <csignal>

#define THREAD_NUM 10

using namespace std;

// �ļ�������Ϣ�ṹ��
struct FileInfo {
    const char* url;          // ����URL
    char* fileptr;            // �ļ��ڴ�ӳ��ָ��
    int offset;               // ��ǰ����ƫ����
    int end;                  // ���ؽ���λ��
    atomic<double> download;  // ��ǰ������
    double totalDownload;     // ��������
    ifstream* recordFile;     // ��¼�ļ���
};

struct FileInfo** pInfoTable; // ȫ���ļ���Ϣ��
atomic<long> downloadFileLength(0);     // �ļ��ܳ���
mutex printMutex;                       // ��ӡ������

// д��ص�����
size_t writeFunc(void* ptr, size_t size, size_t memb, void* userdata) {
    FileInfo* info = static_cast<FileInfo*>(userdata);
    memcpy(info->fileptr + info->offset, ptr, size * memb);
    info->offset += size * memb;
    return size * memb;
}

// ���Ȼص�����
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

// ��ȡ�ļ�����
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

// �����̺߳���
unsigned int __stdcall worker(void* arg) {
    struct FileInfo* info = (struct FileInfo*)arg;
    char range[64] = { 0 };
    
    // �Ӵ浵�ж�ȡ�����صķ�Χ
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

    // ƴ���ַ�����ʾ���ط�Χ
    ostringstream oss;
    oss << info->offset << "-" << info->end;
    strcpy_s(range, oss.str().c_str());
    //snprintf(range, sizeof(range), "%d-%d", info->offset, info->end);

    {
        // ����ȷ����ӡ��ʽ��ȷ
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

// ���غ���
int download(const char* url, const char* filename) {
    // ��ȡ�ļ�����
    long fileLength = getDownloadFileLength(url);
    if (fileLength <= 0) {
        cerr << "Invalid file length." << endl;
        return -1;
    }
    cout << "File length: " << fileLength << " bytes" << endl;

    // ��������ļ�
    HANDLE hFile = CreateFileA(
        filename,
        GENERIC_READ | GENERIC_WRITE, // ��дȨ��
        0,
        NULL,
        OPEN_ALWAYS, // ����ļ��������򴴽�
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        cerr << "Failed to open file: " << filename << endl;
        return -1;
    }

    // �����ļ���С
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

    // �����ļ�ӳ�����
    HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, 0, fileLength, NULL);
    if (hMapping == NULL) {
        cerr << "Failed to create file mapping." << endl;
        CloseHandle(hFile);
        return -1;
    }

    // ���ļ�ӳ�䵽�ڴ�
    char* fileptr = (char*)MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, fileLength);
    if (fileptr == NULL) {
        cerr << "Failed to map view of file." << endl;
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return -1;
    }

    // �򿪼�¼�ļ������ڶϵ�������
    ifstream inFile("record.txt", ios::in);

    // ��ʼ���߳�������Ϣ
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

    // �����̲߳���ʼ����
    HANDLE hThreads[THREAD_NUM + 1] = { NULL };
    for (i = 0; i <= THREAD_NUM; i++) {
        hThreads[i] = (HANDLE)_beginthreadex(NULL, 0, worker, info[i], 0, NULL);
        if (hThreads[i] == NULL) {
            cerr << "Failed to create thread." << endl;
            return -1;
        }
        Sleep(1); // �������ߣ�ȷ���̰߳�˳������
    }

    // �ȴ������߳����
    for (i = 0; i <= THREAD_NUM; i++) {
        WaitForSingleObject(hThreads[i], INFINITE);
        CloseHandle(hThreads[i]);
    }

    // �ͷ���Դ
    for (i = 0; i <= THREAD_NUM; i++) {
        free(info[i]);
    }

    // �����ļ�ӳ��;��
    UnmapViewOfFile(fileptr);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    return 0;
}

// �źŴ�����
void signalHandler(int signum) {
    //cout << "Interrupt signal (" << signum << ") received." << endl;
    cout << "Download stopped." << endl;

    ofstream outFile("record.txt", ios::out | ios::trunc);
    if (!outFile) {
        cerr << "Failed to open record file." << endl;
        exit(1);
    }

    // д��ϵ���Ϣ
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