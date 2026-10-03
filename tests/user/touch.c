#include <fcntl.h>
#include <oscar/stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
    enum { kFileMode = 0644, kWriteCreateFlags = O_WRONLY + O_CREAT };
    static const char usage[] = "touch: usage: touch <file>\n";
    static const char failure[] = "touch: could not create file.\n";
    struct stat status;
    if(argc != 2) {
        oscar_write_string(usage);
        return 1;
    }
    if(stat(argv[1], &status) < 0) {
        const int descriptor = open(argv[1], kWriteCreateFlags, kFileMode);
        if(descriptor < 0 || close(descriptor) < 0) {
            oscar_write_string(failure);
            return 1;
        }
    }
    return 0;
}
