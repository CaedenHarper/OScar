#include <oscar/file.h>
#include <oscar/stdio.h>

int main(int argc, char** argv) {
    static const char usage[] = "cat: usage: cat <file>\n";
    static const char failure[] = "cat: could not read file.\n";
    if(argc != 2) {
        oscar_write_string(usage);
        return 1;
    }
    if(!cat_file(argv[1])) {
        oscar_write_string(failure);
        return 1;
    }
    return 0;
}
