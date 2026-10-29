#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 512
#define NAME_SIZE 100

static void print_windows_error(const char *operation) {
    fprintf(stderr, "%s failed (Windows error %lu)\n", operation, GetLastError());
}

static int write_all(HANDLE pipe, const char *data, DWORD length) {
    DWORD offset = 0;
    while (offset < length) {
        DWORD written = 0;
        if (!WriteFile(pipe, data + offset, length - offset, &written, NULL) || written == 0) {
            return 0;
        }
        offset += written;
    }
    return 1;
}

static int read_line(HANDLE pipe, char *buffer, DWORD capacity) {
    DWORD used = 0;
    while (used + 1 < capacity) {
        char byte;
        DWORD received = 0;
        if (!ReadFile(pipe, &byte, 1, &received, NULL) || received == 0) {
            return 0;
        }
        if (byte == '\n') {
            buffer[used] = '\0';
            if (used > 0 && buffer[used - 1] == '\r') {
                buffer[used - 1] = '\0';
            }
            return 1;
        }
        buffer[used++] = byte;
    }
    buffer[used] = '\0';
    return 0;
}

static int run_worker(void) {
    HANDLE input_pipe = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output_pipe = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input_pipe == INVALID_HANDLE_VALUE || output_pipe == INVALID_HANDLE_VALUE) {
        return 1;
    }

    int student_count = 0;
    double average_total = 0.0;
    char line[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    while (read_line(input_pipe, line, sizeof(line))) {
        if (strcmp(line, "DONE") == 0) {
            double class_average = student_count ? average_total / student_count : 0.0;
            int length = snprintf(response, sizeof(response),
                                  "SUMMARY|%d|%.2f\n", student_count, class_average);
            return length > 0 && write_all(output_pipe, response, (DWORD)length) ? 0 : 1;
        }

        char name[NAME_SIZE];
        float marks[3];
        if (sscanf(line, "%99[^|]|%f|%f|%f", name,
                   &marks[0], &marks[1], &marks[2]) != 4) {
            const char error[] = "ERROR|Could not read student record.\n";
            if (!write_all(output_pipe, error, (DWORD)(sizeof(error) - 1))) return 1;
            continue;
        }

        float average = (marks[0] + marks[1] + marks[2]) / 3.0f;
        const char *grade = average >= 90.0f ? "A" :
                            average >= 80.0f ? "B" :
                            average >= 70.0f ? "C" :
                            average >= 60.0f ? "D" :
                            average >= 50.0f ? "E" : "F";
        const char *status = marks[0] >= 40.0f && marks[1] >= 40.0f && marks[2] >= 40.0f
                                 ? "PASS" : "FAIL";

        int length = snprintf(response, sizeof(response), "RESULT|%s|%.2f|%s|%s\n",
                              name, average, grade, status);
        if (length <= 0 || (size_t)length >= sizeof(response) ||
            !write_all(output_pipe, response, (DWORD)length)) {
            return 1;
        }
        student_count++;
        average_total += average;
    }
    return 1;
}

static void remove_line_ending(char *text) {
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
}

static int read_mark(const char *subject, float *mark) {
    char input[BUFFER_SIZE];
    for (;;) {
        printf("  Enter %s mark (0-100): ", subject);
        if (!fgets(input, sizeof(input), stdin)) return 0;
        char *end = NULL;
        float value = strtof(input, &end);
        while (end && isspace((unsigned char)*end)) end++;
        if (end != input && end && *end == '\0' && value >= 0.0f && value <= 100.0f) {
            *mark = value;
            return 1;
        }
        puts("  Please enter a number from 0 to 100.");
    }
}

static int run_parent(void) {
    SECURITY_ATTRIBUTES security = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE to_child_read = NULL, to_child_write = NULL;
    HANDLE from_child_read = NULL, from_child_write = NULL;

    if (!CreatePipe(&to_child_read, &to_child_write, &security, 0) ||
        !CreatePipe(&from_child_read, &from_child_write, &security, 0)) {
        print_windows_error("CreatePipe");
        return 1;
    }
    if (!SetHandleInformation(to_child_write, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(from_child_read, HANDLE_FLAG_INHERIT, 0)) {
        print_windows_error("SetHandleInformation");
        return 1;
    }

    char executable_path[MAX_PATH];
    DWORD path_length = GetModuleFileNameA(NULL, executable_path, MAX_PATH);
    if (path_length == 0 || path_length >= MAX_PATH) {
        print_windows_error("GetModuleFileName");
        return 1;
    }
    executable_path[path_length] = '\0';

    char command_line[MAX_PATH + 32];
    int command_length = snprintf(command_line, sizeof(command_line),
                                  "\"%s\" --worker", executable_path);
    if (command_length <= 0 || (size_t)command_length >= sizeof(command_line)) {
        fprintf(stderr, "Executable path is too long.\n");
        return 1;
    }

    STARTUPINFOA startup = { 0 };
    PROCESS_INFORMATION process = { 0 };
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = to_child_read;
    startup.hStdOutput = from_child_write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessA(executable_path, command_line, NULL, NULL, TRUE, 0,
                        NULL, NULL, &startup, &process)) {
        print_windows_error("CreateProcess");
        return 1;
    }

    CloseHandle(to_child_read);
    CloseHandle(from_child_write);
    puts("Student Record Processor");
    puts("Enter each student's name and three marks. Type 'done' for the name when finished.\n");

    int failed = 0;
    for (;;) {
        char name[NAME_SIZE];
        float marks[3];
        printf("Student name (or done): ");
        if (!fgets(name, sizeof(name), stdin)) break;
        remove_line_ending(name);

        if (_stricmp(name, "done") == 0) break;
        if (name[0] == '\0' || strchr(name, '|') != NULL) {
            puts("Please enter a non-empty name without the '|' character.\n");
            continue;
        }
        if (!read_mark("Math", &marks[0]) ||
            !read_mark("Science", &marks[1]) ||
            !read_mark("English", &marks[2])) {
            failed = 1;
            break;
        }

        char record[BUFFER_SIZE];
        int length = snprintf(record, sizeof(record), "%s|%.2f|%.2f|%.2f\n",
                              name, marks[0], marks[1], marks[2]);
        if (length <= 0 || (size_t)length >= sizeof(record) ||
            !write_all(to_child_write, record, (DWORD)length)) {
            print_windows_error("Sending student record");
            failed = 1;
            break;
        }

        char reply[BUFFER_SIZE];
        if (!read_line(from_child_read, reply, sizeof(reply))) {
            print_windows_error("Receiving student result");
            failed = 1;
            break;
        }
        if (strncmp(reply, "RESULT|", 7) == 0) {
            char result_name[NAME_SIZE], grade[8], status[8];
            float average;
            if (sscanf(reply, "RESULT|%99[^|]|%f|%7[^|]|%7s",
                       result_name, &average, grade, status) == 4) {
                printf("\nResult for %s\n  Average: %.2f\n  Grade: %s\n  Status: %s\n\n",
                       result_name, average, grade, status);
            } else {
                printf("Worker response: %s\n", reply);
            }
        } else {
            printf("Worker response: %s\n\n", reply);
        }
    }

    const char done_message[] = "DONE\n";
    if (!failed && !write_all(to_child_write, done_message, (DWORD)(sizeof(done_message) - 1))) {
        print_windows_error("Sending end-of-input message");
        failed = 1;
    }
    CloseHandle(to_child_write);

    if (!failed) {
        char summary[BUFFER_SIZE];
        if (read_line(from_child_read, summary, sizeof(summary))) {
            int count = 0;
            double class_average = 0.0;
            if (sscanf(summary, "SUMMARY|%d|%lf", &count, &class_average) == 2) {
                printf("Class summary: %d student(s), class average %.2f\n", count, class_average);
            }
        } else {
            print_windows_error("Receiving class summary");
            failed = 1;
        }
    }

    CloseHandle(from_child_read);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD child_exit_code = 1;
    GetExitCodeProcess(process.hProcess, &child_exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return failed || child_exit_code != 0 ? 1 : 0;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--worker") == 0) {
        return run_worker();
    }
    return run_parent();
}

