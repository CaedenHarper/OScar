#include <oscar/stdio.h>
#include <oscar/string.h>
#include <oscar/syscalls.h>
#include <stdint.h>

int main(int argc, char** argv) {
    static const char usage[] = "kill: usage: kill <pid>\n";
    static const char failure[] = "kill: could not terminate process.\n";
    int64_t process_id = 0;
    if(argc != 2 || !oscar_parse_i64(argv[1], &process_id) || process_id <= 0 || oscar_kill((uint64_t)process_id) < 0) {
        oscar_write_string(argc == 2 ? failure : usage);
        return 1;
    }
    return 0;
}
