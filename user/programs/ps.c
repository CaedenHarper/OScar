#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>

int main(int argc, char** argv) {
    static const char usage[] = "ps: usage: ps\n";
    static const char header[] = "PID STATE THREADS PAGES IMAGE\n";
    (void)argv;
    if(argc != 1) {
        oscar_write_string(usage);
        return 1;
    }
    oscar_write_string(header);
    for(uint64_t index = 0;; ++index) {
        struct oscar_process_info info;
        const int64_t result = oscar_get_process_info(index, &info);
        if(result == OSCAR_ERROR_NOT_FOUND) {
            return 0;
        }
        if(result < 0) {
            oscar_write_string("ps: could not read process list.\n");
            return 1;
        }
        oscar_write_uint(info.id);
        const char* state_text = " new ";
        if(info.state == OSCAR_PROCESS_RUNNING) {
            state_text = " run ";
        } else if(info.state == OSCAR_PROCESS_TERMINATED) {
            state_text = " done ";
        }
        oscar_write_string(state_text);
        oscar_write_uint(info.thread_count);
        oscar_write_string(" ");
        oscar_write_uint(info.user_page_count);
        oscar_write_string(" ");
        oscar_write_string(info.image_path);
        oscar_write_string("\n");
    }
}
