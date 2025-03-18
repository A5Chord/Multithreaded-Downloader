#include "download.h"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <URL> <output file>" << endl;
        return -1;
    }

    signal(SIGINT, signalHandler);

    return download(argv[1], argv[2]);
}