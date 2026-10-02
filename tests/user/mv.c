#include <oscar/file.h>
#include <oscar/stdio.h>
#include <oscar/syscalls.h>

int main(int argc, char** argv) {
    static const char usage[] = "mv: usage: mv <source> <destination>\n";
    static const char failure[] = "mv: could not move file.\n";
    if(argc != 3) {
        oscar_write_string(usage);
        return 1;
    }
    if(!oscar_copy_file(argv[1], argv[2]) || oscar_unlink(argv[1]) < 0) {
        oscar_write_string(failure);
        return 1;
    }
    return 0;
}
