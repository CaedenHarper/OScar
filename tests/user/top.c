#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay)

enum {
    kMaximumProcesses = 32,
    kRefreshCount = 5,
    kRefreshTicks = 20,
};

static uint64_t string_length(const char* string) {
    uint64_t length = 0;
    while(string[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_string(const char* string) {
    (void)oscar_write(1, string, string_length(string));
}

static void write_number(uint64_t value) {
    char digits[20];
    uint32_t length = 0;
    do {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while(value != 0);
    while(length != 0) {
        --length;
        (void)oscar_write(1, &digits[length], 1);
    }
}

static void write_state(uint32_t state) {
    static const char new_state[] = "new";
    static const char running_state[] = "run";
    static const char terminated_state[] = "done";
    static const char unknown_state[] = "?";
    if(state == OSCAR_PROCESS_NEW) {
        write_string(new_state);
    } else if(state == OSCAR_PROCESS_RUNNING) {
        write_string(running_state);
    } else if(state == OSCAR_PROCESS_TERMINATED) {
        write_string(terminated_state);
    } else {
        write_string(unknown_state);
    }
}

static void print_process(const struct oscar_process_info* info) {
    write_number(info->id);
    write_string("\t");
    write_state(info->state);
    write_string("\t");
    write_number(info->thread_count);
    write_string("\t");
    write_number(info->user_page_count * 4);
    write_string("K\t");
    write_string(info->image_path);
    write_string("\n");
}

static void print_snapshot(void) {
    static const char clear_screen[] = "\033[2J\033[H";
    static const char header[] = "PID\tSTATE\tTHREADS\tMEM\tIMAGE\n";
    static const char failure[] = "top: could not read process table.\n";
    (void)oscar_write(1, clear_screen, sizeof(clear_screen) - 1);
    write_string("OScar top\n\n");
    write_string(header);

    for(uint64_t index = 0; index < kMaximumProcesses; ++index) {
        struct oscar_process_info info;
        const int64_t result = oscar_get_process_info(index, &info);
        if(result == OSCAR_ERROR_NOT_FOUND) {
            return;
        }
        if(result < 0) {
            write_string(failure);
            return;
        }
        print_process(&info);
    }
}

void _start(void) {
    for(uint32_t refresh = 0; refresh < kRefreshCount; ++refresh) {
        print_snapshot();
        oscar_sleep(kRefreshTicks);
    }
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
