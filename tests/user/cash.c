#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay)

enum {
    kLineCapacity = 128,
    kMaximumArguments = 8,
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
    static const char prefix[] = "cash:";
    static const char suffix[] = "$ ";
    static const char fallback[] = "cash:?$ ";
    static char directory[kLineCapacity];
    const int64_t length = oscar_getcwd(directory, sizeof(directory));
    if(length < 0) {
        write_string(fallback);
        return;
    }
    write_string(prefix);
    (void)oscar_write(1, directory, (uint64_t)length);
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

static int run_exit(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: exit [status]\n";
    int64_t status = 0;
    if(argument_count > 2 || (argument_count == 2 && !parse_status(arguments[1], &status))) {
        write_string(usage);
        return 0;
    }
    oscar_exit(status);
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
    } else if(compare_strings(arguments[0], "exit")) {
        (void)run_exit(arguments, argument_count);
    } else {
        write_string("cash: command not found: ");
        write_string(arguments[0]);
        write_string("\n");
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
