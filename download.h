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

extern struct FileInfo** pInfoTable; // ȫ���ļ���Ϣ��
extern atomic<long> downloadFileLength;     // �ļ��ܳ���
extern mutex printMutex;                       // ��ӡ������

// д��ص�����
size_t writeFunc(void* ptr, size_t size, size_t memb, void* userdata);

// ���Ȼص�����
int progressFunc(void* userdata, double totalDownload, double nowDownload, double, double);

// ��ȡ�ļ�����
long getDownloadFileLength(const char* url);

// �����̺߳���
unsigned int __stdcall worker(void* arg);

// ���غ���
int download(const char* url, const char* filename);

// �źŴ�����
void signalHandler(int signum);

#endif // DOWNLOAD_H