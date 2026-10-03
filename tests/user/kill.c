#include <oscar/stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

int main(int argc, char** argv) {
    static const char usage[] = "kill: usage: kill <pid>\n";
    static const char failure[] = "kill: could not terminate process.\n";
    char* end = (char*)0;
    const long process_id = argc == 2 ? strtol(argv[1], &end, 10) : 0;
    if(argc != 2 || end == argv[1] || *end != '\0' || process_id <= 0 || kill((pid_t)process_id, SIGTERM) < 0) {
        oscar_write_string(argc == 2 ? failure : usage);
        return 1;
    }
    return 0;
}
