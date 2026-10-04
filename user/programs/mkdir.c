#include <oscar/stdio.h>
#include <unistd.h>

int main(int argc, char** argv) {
    enum { kDirectoryMode = 0755 };
    static const char usage[] = "mkdir: usage: mkdir <directory>\n";
    static const char failure[] = "mkdir: could not create directory.\n";
    if(argc != 2) {
        oscar_write_string(usage);
        return 1;
    }
    if(mkdir(argv[1], kDirectoryMode) < 0) {
        oscar_write_string(failure);
        return 1;
    }
    return 0;
}
