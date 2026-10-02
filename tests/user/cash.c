#include <oscar/stdio.h>
#include <oscar/string.h>
#include <oscar/syscalls.h>
#include <stdint.h>

enum {
    kLineCapacity = 128,
    kMaximumArguments = 16,
    kCommandPathCapacity = 128,
};

typedef void (*BuiltinHandler)(char* const arguments[], uint32_t argument_count);

struct Builtin {
    const char* name;
    BuiltinHandler handler;
};

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

    oscar_write_string(shell_color);
    oscar_write_string(shell_name);
    oscar_write_string(reset_color);
    oscar_write_string(colon);
    oscar_write_string(directory_color);
    if(length < 0) {
        oscar_write_string(fallback_directory);
    } else {
        (void)oscar_write(1, directory, (uint64_t)length);
    }
    oscar_write_string(reset_color);
    oscar_write_string(suffix);
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

static void run_pwd(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: pwd\n";
    static const char failure[] = "cash: pwd: could not read working directory.\n";
    static char directory[kLineCapacity];
    (void)arguments;
    if(argument_count != 1) {
        oscar_write_string(usage);
        return;
    }
    const int64_t length = oscar_getcwd(directory, sizeof(directory));
    if(length < 0) {
        oscar_write_string(failure);
        return;
    }
    (void)oscar_write(1, directory, (uint64_t)length);
    oscar_write_string("\n");
}

static void run_cd(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: cd <directory>\n";
    static const char failure[] = "cash: cd: could not change directory.\n";
    if(argument_count != 2) {
        oscar_write_string(usage);
        return;
    }
    if(oscar_chdir(arguments[1]) < 0) {
        oscar_write_string(failure);
    }
}

static void run_echo(char* const arguments[], uint32_t argument_count) {
    for(uint32_t index = 1; index < argument_count; ++index) {
        if(index != 1) {
            oscar_write_string(" ");
        }
        oscar_write_string(arguments[index]);
    }
    oscar_write_string("\n");
}

static void run_help(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: help\n";
    static const char commands[] = "built-ins: cd pwd echo help exit\n"
                                   "filesystem: mkdir touch cp mv rm cat ls df du mount\n"
                                   "processes: ps top kill\n"
                                   "network: ping <ipv4-address-or-hostname>, nslookup <hostname>,\n"
                                   "          httpget <http-url>\n";
    (void)arguments;
    if(argument_count != 1) {
        oscar_write_string(usage);
        return;
    }
    oscar_write_string(commands);
}

static void run_exit(char* const arguments[], uint32_t argument_count) {
    static const char usage[] = "cash: usage: exit [status]\n";
    int64_t status = 0;
    if(argument_count > 2 || (argument_count == 2 && !oscar_parse_i64(arguments[1], &status))) {
        oscar_write_string(usage);
        return;
    }
    oscar_exit(status);
}

static const struct Builtin kBuiltins[] = {
    {"pwd", run_pwd},
    {"cd", run_cd},
    {"echo", run_echo},
    {"help", run_help},
    {"exit", run_exit},
};

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
        oscar_write_string(failure);
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
        oscar_write_string(failure);
    } else if(status != 0) {
        oscar_write_string(exited_failure);
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

    for(uint64_t index = 0; index < sizeof(kBuiltins) / sizeof(kBuiltins[0]); ++index) {
        if(oscar_streq(arguments[0], kBuiltins[index].name)) {
            kBuiltins[index].handler(arguments, argument_count);
            return;
        }
    }
    run_external(arguments, argument_count);
}

int main(void) {
    static const char read_failure[] = "cash: input read failed.\n";
    static char line[kLineCapacity];

    for(;;) {
        write_prompt();
        const int64_t count = oscar_read(0, line, sizeof(line));
        if(count < 0) {
            (void)oscar_write(1, read_failure, sizeof(read_failure) - 1);
            oscar_exit(0);
        }
        if(count == 0) {
            continue;
        }
        execute_line(line, (uint64_t)count);
    }
}
