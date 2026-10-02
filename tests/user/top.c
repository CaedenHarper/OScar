#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>

enum {
    kMaximumProcesses = 32,
    kRefreshCount = 5,
    kRefreshTicks = 20,
};

static void write_state(uint32_t state) {
    static const char new_state[] = "new";
    static const char running_state[] = "run";
    static const char terminated_state[] = "done";
    static const char unknown_state[] = "?";
    if(state == OSCAR_PROCESS_NEW) {
        oscar_write_string(new_state);
    } else if(state == OSCAR_PROCESS_RUNNING) {
        oscar_write_string(running_state);
    } else if(state == OSCAR_PROCESS_TERMINATED) {
        oscar_write_string(terminated_state);
    } else {
        oscar_write_string(unknown_state);
    }
}

static void print_process(const struct oscar_process_info* info) {
    oscar_write_uint(info->id);
    oscar_write_string("\t");
    write_state(info->state);
    oscar_write_string("\t");
    oscar_write_uint(info->thread_count);
    oscar_write_string("\t");
    oscar_write_uint(info->user_page_count * 4);
    oscar_write_string("K\t");
    oscar_write_string(info->image_path);
    oscar_write_string("\n");
}

static void print_snapshot(void) {
    static const char clear_screen[] = "\033[2J\033[H";
    static const char header[] = "PID\tSTATE\tTHREADS\tMEM\tIMAGE\n";
    static const char failure[] = "top: could not read process table.\n";
    (void)oscar_write(1, clear_screen, sizeof(clear_screen) - 1);
    oscar_write_string("OScar top\n\n");
    oscar_write_string(header);

    for(uint64_t index = 0; index < kMaximumProcesses; ++index) {
        struct oscar_process_info info;
        const int64_t result = oscar_get_process_info(index, &info);
        if(result == OSCAR_ERROR_NOT_FOUND) {
            return;
        }
        if(result < 0) {
            oscar_write_string(failure);
            return;
        }
        print_process(&info);
    }
}

int main(void) {
    for(uint32_t refresh = 0; refresh < kRefreshCount; ++refresh) {
        print_snapshot();
        oscar_sleep(kRefreshTicks);
    }
    oscar_exit(0);
}
