#include <oscar/stdio.h>
#include <unistd.h>

int main(int argc, char** argv) {
    static const char usage[] = "rm: usage: rm <file>\n";
    static const char failure[] = "rm: could not remove file.\n";
    if(argc != 2) {
        oscar_write_string(usage);
        return 1;
    }
    if(unlink(argv[1]) < 0) {
        oscar_write_string(failure);
        return 1;
    }
    return 0;
}
