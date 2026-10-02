#include <oscar/file.h>
#include <oscar/stdio.h>

int main(int argc, char** argv) {
    static const char usage[] = "cp: usage: cp <source> <destination>\n";
    static const char failure[] = "cp: could not copy file.\n";
    if(argc != 3) {
        oscar_write_string(usage);
        return 1;
    }
    if(!oscar_copy_file(argv[1], argv[2])) {
        oscar_write_string(failure);
        return 1;
    }
    return 0;
}
