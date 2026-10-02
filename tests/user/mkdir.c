#include <oscar/stdio.h>
#include <oscar/syscalls.h>

int main(int argc, char** argv) {
    static const char usage[] = "mkdir: usage: mkdir <directory>\n";
    static const char failure[] = "mkdir: could not create directory.\n";
    if(argc != 2) {
        oscar_write_string(usage);
        return 1;
    }
    if(oscar_mkdir(argv[1]) < 0) {
        oscar_write_string(failure);
        return 1;
    }
    return 0;
}
