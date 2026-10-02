#include <oscar/stdio.h>
#include <oscar/syscalls.h>

int main(int argc, char** argv) {
    static const char usage[] = "touch: usage: touch <file>\n";
    static const char failure[] = "touch: could not create file.\n";
    struct oscar_stat status;
    if(argc != 2) {
        oscar_write_string(usage);
        return 1;
    }
    if(oscar_stat(argv[1], &status) < 0 && oscar_create(argv[1]) < 0) {
        oscar_write_string(failure);
        return 1;
    }
    return 0;
}
