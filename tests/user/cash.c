#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay)

enum {
    kLineCapacity = 128,
    kMaximumArguments = 16,
    kCommandPathCapacity = 128,
    kIoBufferCapacity = 256,
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

static void write_prompt(void) {
    static const char shell_color[] = "\033[1;36m";
    static const char directory_color[] = "\033[1;34m";
    static const char reset_color[] = "\033[0m";
    static const char shell_name[] = "cash";
    static const char colon[] = ":";
    static const char suffix[] = "$ ";
    static const char fallback_directory[] = "?";
    static char directory[kLineCapacity];
    const int64_t length = oscar_getcwd(directory, sizeof(directory));

    write_string(shell_color);
    write_string(shell_name);
    write_string(reset_color);
    write_string(colon);
    write_string(directory_color);
    if(length < 0) {
        write_string(fallback_directory);
    } else {
        (void)oscar_write(1, directory, (uint64_t)length);
    }
    write_string(reset_color);
    write_string(suffix);
}

static int compare_strings(const char* left, const char* right) {
    uint64_t index = 0;
    while(left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

static int is_separator(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

static uint32_t tokenize(char* line, char* arguments[], uint32_t capacity) {
    uint32_t count = 0;
    uint32_t index = 0;
    while(line[index] != '\0') {
        while(line[index] != '\0' && is_separator(line[index])) {
            line[index++] = '\0';
        }
        if(line[index] == '\0' || count == capacity) {
            break;
        }
        arguments[count++] = &line[index];
        while(line[index] != '\0' && !is_separator(line[index])) {
            ++index;
        }
    }
    return count;
}

static int parse_status(const char* text, int64_t* status) {
    uint64_t index = 0;
    uint64_t value = 0;
    int negative = 0;
    if(text[0] == '-') {
        negative = 1;
        index = 1;
    }
    if(text[index] == '\0') {
        return 0;
    }
    while(text[index] != '\0') {
        if(text[index] < '0' || text[index] > '9') {
            return 0;
        }
        value = value * 10 + (uint64_t)(text[index] - '0');
        ++index;
    }
    *status = negative ? -(int64_t)value : (int64_t)value;
    return 1;
}

static void run_pwd(uint32_t argument_count) {
    static const char usage[] = "cash: usage: pwd\n";
    static const char failure[] = "cash: pwd: could not read working directory.\n";
    static char directory[kLineCapacity];
    if(argument_count != 1) {
        write_string(usage);
        return;
    }
    const int64_t length = oscar_getcwd(directory, sizeof(directory));
    if(length < 0) {
        write_string(failure);
        return;
    }
    (void)oscar_write(1, directory, (uint64_t)length);
    write_string("\n");
}

static void run_cd(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: cd <directory>\n";
    static const char failure[] = "cash: cd: could not change directory.\n";
    if(argument_count != 2) {
        write_string(usage);
        return;
    }
    if(oscar_chdir(arguments[1]) < 0) {
        write_string(failure);
    }
}

static void run_echo(char* const arguments[], uint32_t argument_count) {
    for(uint32_t index = 1; index < argument_count; ++index) {
        if(index != 1) {
            write_string(" ");
        }
        write_string(arguments[index]);
    }
    write_string("\n");
}

static void run_help(uint32_t argument_count) {
    static const char usage[] = "cash: usage: help\n";
    static const char commands[] = "built-ins: cd pwd echo help exit\n"
                                   "filesystem: mkdir touch cp mv rm cat ls df du mount\n"
                                   "processes: ps top kill\n"
                                   "network: ping <ipv4-address>\n";
    if(argument_count != 1) {
        write_string(usage);
        return;
    }
    write_string(commands);
}

static void run_mkdir(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: mkdir <directory>\n";
    static const char failure[] = "cash: mkdir: could not create directory.\n";
    if(argument_count != 2 || oscar_mkdir(arguments[1]) < 0) {
        write_string(argument_count == 2 ? failure : usage);
    }
}

static void run_touch(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: touch <file>\n";
    static const char failure[] = "cash: touch: could not create file.\n";
    struct oscar_stat status;
    if(argument_count != 2) {
        write_string(usage);
        return;
    }
    if(oscar_stat(arguments[1], &status) < 0 && oscar_create(arguments[1]) < 0) {
        write_string(failure);
    }
}

static void run_rm(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: rm <file>\n";
    static const char failure[] = "cash: rm: could not remove file.\n";
    if(argument_count != 2 || oscar_unlink(arguments[1]) < 0) {
        write_string(argument_count == 2 ? failure : usage);
    }
}

static int copy_file(const char* source, const char* destination) {
    const int64_t input = oscar_open(source, OSCAR_OPEN_READ);
    if(input < 0) {
        return 0;
    }

    int64_t output = oscar_open(destination, OSCAR_OPEN_WRITE);
    if(output < 0 && oscar_create(destination) == 0) {
        output = oscar_open(destination, OSCAR_OPEN_WRITE);
    }
    if(output < 0) {
        (void)oscar_close(input);
        return 0;
    }

    char buffer[kIoBufferCapacity];
    int success = 1;
    for(;;) {
        const int64_t received = oscar_read(input, buffer, sizeof(buffer));
        if(received < 0) {
            success = 0;
            break;
        }
        if(received == 0) {
            break;
        }
        const int64_t written = oscar_write(output, buffer, (uint64_t)received);
        if(written != received) {
            success = 0;
            break;
        }
    }
    (void)oscar_close(input);
    (void)oscar_close(output);
    return success;
}

static void run_cp(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: cp <source> <destination>\n";
    static const char failure[] = "cash: cp: could not copy file.\n";
    if(argument_count != 3) {
        write_string(usage);
        return;
    }
    if(!copy_file(arguments[1], arguments[2])) {
        write_string(failure);
    }
}

static void run_mv(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: mv <source> <destination>\n";
    static const char failure[] = "cash: mv: could not move file.\n";
    if(argument_count != 3) {
        write_string(usage);
        return;
    }
    if(!copy_file(arguments[1], arguments[2]) || oscar_unlink(arguments[1]) < 0) {
        write_string(failure);
    }
}

static void run_cat(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: cat <file>\n";
    static const char failure[] = "cash: cat: could not read file.\n";
    if(argument_count != 2) {
        write_string(usage);
        return;
    }
    const int64_t input = oscar_open(arguments[1], OSCAR_OPEN_READ);
    if(input < 0) {
        write_string(failure);
        return;
    }
    char buffer[kIoBufferCapacity];
    int success = 1;
    for(;;) {
        const int64_t received = oscar_read(input, buffer, sizeof(buffer));
        if(received < 0) {
            success = 0;
            break;
        }
        if(received == 0) {
            break;
        }
        if(oscar_write(1, buffer, (uint64_t)received) != received) {
            success = 0;
            break;
        }
    }
    (void)oscar_close(input);
    if(!success) {
        write_string(failure);
    }
}

static void write_number(uint64_t value) {
    char digits[20];
    uint32_t length = 0;
    do {
        digits[length++] = (char)('0' + (value % 10));
        value /= 10;
    } while(value != 0);
    while(length != 0) {
        (void)oscar_write(1, &digits[--length], 1);
    }
}

static void run_ps(uint32_t argument_count) {
    static const char usage[] = "cash: usage: ps\n";
    static const char header[] = "PID STATE THREADS PAGES IMAGE\n";
    if(argument_count != 1) {
        write_string(usage);
        return;
    }
    write_string(header);
    for(uint64_t index = 0;; ++index) {
        struct oscar_process_info info;
        const int64_t result = oscar_get_process_info(index, &info);
        if(result == OSCAR_ERROR_NOT_FOUND) {
            return;
        }
        if(result < 0) {
            write_string("cash: ps: could not read process list.\n");
            return;
        }
        write_number(info.id);
        write_string(
            info.state == OSCAR_PROCESS_RUNNING      ? " run "
            : info.state == OSCAR_PROCESS_TERMINATED ? " done "
                                                     : " new "
        );
        write_number(info.thread_count);
        write_string(" ");
        write_number(info.user_page_count);
        write_string(" ");
        write_string(info.image_path);
        write_string("\n");
    }
}

static void run_kill(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: kill <pid>\n";
    static const char failure[] = "cash: kill: could not terminate process.\n";
    int64_t process_id = 0;
    if(argument_count != 2 || !parse_status(arguments[1], &process_id) || process_id <= 0 ||
       oscar_kill((uint64_t)process_id) < 0) {
        write_string(argument_count == 2 ? failure : usage);
    }
}

static int run_exit(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: exit [status]\n";
    int64_t status = 0;
    if(argument_count > 2 || (argument_count == 2 && !parse_status(arguments[1], &status))) {
        write_string(usage);
        return 0;
    }
    oscar_exit(status);
}

static int build_command_path(const char* command, char* path) {
    uint64_t path_index = 0;
    uint64_t command_index = 0;
    if(command[0] == '/') {
        while(command[command_index] != '\0' && path_index + 1 < kCommandPathCapacity) {
            path[path_index++] = command[command_index++];
        }
    } else {
        static const char prefix[] = "/bin/";
        while(prefix[path_index] != '\0' && path_index + 1 < kCommandPathCapacity) {
            path[path_index] = prefix[path_index];
            ++path_index;
        }
        command_index = 0;
        while(command[command_index] != '\0' && path_index + 1 < kCommandPathCapacity) {
            path[path_index++] = command[command_index++];
        }
    }
    if(command[command_index] != '\0') {
        return 0;
    }
    path[path_index] = '\0';
    return 1;
}

static void run_external(char* const arguments[], uint32_t argument_count) {
    static const char failure[] = "cash: could not start command.\n";
    static const char exited_failure[] = "cash: command exited unsuccessfully.\n";
    char path[kCommandPathCapacity];
    if(!build_command_path(arguments[0], path)) {
        write_string(failure);
        return;
    }
    const char* child_arguments[kMaximumArguments + 1];
    for(uint32_t index = 0; index < argument_count; ++index) {
        child_arguments[index] = arguments[index];
    }
    child_arguments[argument_count] = 0;
    const int64_t child_id = oscar_spawn_args(path, child_arguments);
    int64_t status = 0;
    if(child_id < 0 || oscar_waitpid((uint64_t)child_id, &status) < 0) {
        write_string(failure);
    } else if(status != 0) {
        write_string(exited_failure);
    }
}

static void execute_line(char* line, uint64_t length) {
    char* arguments[kMaximumArguments];
    const uint64_t terminator = length < kLineCapacity ? length : kLineCapacity - 1;
    line[terminator] = '\0';
    const uint32_t argument_count = tokenize(line, arguments, kMaximumArguments);
    if(argument_count == 0) {
        return;
    }

    if(compare_strings(arguments[0], "pwd")) {
        run_pwd(argument_count);
    } else if(compare_strings(arguments[0], "cd")) {
        run_cd(arguments, argument_count);
    } else if(compare_strings(arguments[0], "echo")) {
        run_echo(arguments, argument_count);
    } else if(compare_strings(arguments[0], "help")) {
        run_help(argument_count);
    } else if(compare_strings(arguments[0], "exit")) {
        (void)run_exit(arguments, argument_count);
    } else if(compare_strings(arguments[0], "mkdir")) {
        run_mkdir(arguments, argument_count);
    } else if(compare_strings(arguments[0], "touch")) {
        run_touch(arguments, argument_count);
    } else if(compare_strings(arguments[0], "cp")) {
        run_cp(arguments, argument_count);
    } else if(compare_strings(arguments[0], "mv")) {
        run_mv(arguments, argument_count);
    } else if(compare_strings(arguments[0], "rm")) {
        run_rm(arguments, argument_count);
    } else if(compare_strings(arguments[0], "cat")) {
        run_cat(arguments, argument_count);
    } else if(compare_strings(arguments[0], "ps")) {
        run_ps(argument_count);
    } else if(compare_strings(arguments[0], "kill")) {
        run_kill(arguments, argument_count);
    } else {
        run_external(arguments, argument_count);
    }
}

__attribute__((noreturn)) static void exit_shell(void) {
    oscar_exit(0);
}

void _start(void) {
    static const char read_failure[] = "cash: input read failed.\n";
    static char line[kLineCapacity];

    for(;;) {
        write_prompt();
        const int64_t count = oscar_read(0, line, sizeof(line));
        if(count < 0) {
            (void)oscar_write(1, read_failure, sizeof(read_failure) - 1);
            exit_shell();
        }
        if(count == 0) {
            continue;
        }

        execute_line(line, (uint64_t)count);
    }
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
