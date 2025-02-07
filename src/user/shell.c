// shell.c

#include "syscall.h"
#include "string.h"
#include "io.h"
#include "../kern/kfs.h"
#include "../kern/process.h"
#include "../kern/signals.h"

// constants
#define MAX_INPUT 64
#define MAX_HISTORY 10
#define MAX_ARGS 10

// global variables
char history[MAX_HISTORY][MAX_INPUT];
char input[MAX_INPUT];
int buf_size, cursor_pos, history_count, history_pos;
static int device_counter = 1;

// internal function declerations
void handle_arrow_keys(char c);
void handle_new_line();
void handle_backspace_char();
void fetch_history(int direction);
void save_to_history(const char * input);
void tokenize_input(char * input_copy, char *argv[], int *argc);
void list_programs(int num_programs);
int run_program(char *program);
void print_help();
void execute_command(int argc, char *argv[]);
void list_processes();
int get_sig_num(char * sig_name);
int parse_int(const char *str);



int main() {
    int len, result;
    char c;

    // this loop checks if the user has opened the shell screen 
    while (1) {
        // simply check for keyboard input and only after start the shell
        result = _read(0, &c, 1);

        if (result) {
            _write(0, "> ", sizeof("> "));
            break; 
        }
    }

    buf_size = 0;
    cursor_pos = 0;
    history_count = 0;
    history_pos = -1; 

    // this loop actually reads the screen
    while(1) {

        // read a single char
        // check if its one of the arrow keys
        //      a. left/right arrow: move through the input array
        //      want to make it similar to actual bash so it appends instead of 
        //      overwriting
        //      b. up/down arrow: fetch history
        // if its a newline character
        //      a. tokenize the input and check for a command
        //      b. save the current input (if there is one)
        //      c. print a new line and restart the loop
        // if its a backspace character
        //      a. remove the char from the buffer
        //      b. decrement the buf size counter
        //      c. erase the output on screen by: moving back, printing a screen,
        //      and moving the cursor back 
        // else print that single char to the screen and append it to the buffer
        // if input buffer is full, ignore the new characters

        // read a char
        len = _read(0, &c, 1);

        if (len < 0) {
            _msgout("error encountered.");
            _exit();
        }

        // handle arrow keys
        if (c == '\x1b') {
            char seq[2];

            _read(0, &seq[0], 1);   // read '['
            _read(0, &seq[1], 1);   // read 'D' or 'C'

            if (seq[0] == '[') {
                handle_arrow_keys(seq[1]);
                continue;
            } 
        }

        // handle new line char
        if (c == '\r') {
            int argc;
            char *argv[MAX_ARGS];
            char input_copy[MAX_INPUT];
            memcpy(input_copy, input, MAX_INPUT);

            tokenize_input(input_copy, argv, &argc);
            execute_command(argc, argv);

            handle_new_line();

            // debug
            for (int i = 0; i < MAX_ARGS; i++) {
                _msgout(argv[i]);
            }

            // reset for the next command
            history_pos = -1;
            buf_size = 0;
            cursor_pos = 0;
            memset(input, 0, MAX_INPUT);

            continue;
        }

        // handle backspace char
        if (c == 127) {
            if (cursor_pos > 0 && buf_size > 0) {
                handle_backspace_char();
            }

            continue;
        }

        if (buf_size >= MAX_INPUT - 1) {
            // ignore new characters if we reached the buffer size
            continue;
        }

        // insert the character at the cursor position
        for (int i = buf_size; i > cursor_pos; i--) {
            input[i] = input[i - 1];
        }

        input[cursor_pos] = c;
        buf_size++;
        cursor_pos++;

        // echo the character to the screen 
        _write(0, &input[cursor_pos - 1], 1);

        // reprint characters after the cursor
        _write(0, &input[cursor_pos], buf_size - cursor_pos);
        for (int i = cursor_pos; i < buf_size; i++) {
            _write(0, "\b", 1);  // move cursor back to the correct position
        }
    }
}



void handle_arrow_keys(char c) {
    if (c == 'D' && cursor_pos > 0) {  // left arrow key
        cursor_pos--;
        _write(0, "\x1b[D", 3);  // move cursor left
    } else if (c == 'C' && cursor_pos < buf_size) {    // right arrow key
        cursor_pos++; 
        _write(0, "\x1b[C", 3); // move cursor right
    } else if (c == 'B') {  // down arrow key
        fetch_history(-1);  // -1 is for down key
    } else if (c == 'A') { // up arrow key
        fetch_history(1);   // 1 is for up key
    }
}



void fetch_history(int direction) {
    // when fetching history
    // if the input is an up arrow
    //      - if we are at the last history; ignore input
    //      - if we are at input -1 (ie no input); change input to 0
    //      - else move up in inputs
    //
    // if the input is a down arrow
    //      - if we are at input 0; clear line 
    //      - if we are at input -1 (ie no input); ignore input 
    //      - else move down in inputs
    //
    // if the input is valid (between 0 and MAX_HISTORY) display the command history
    //      - set input to history
    //      - reprint the line; set bufsize to history command bufsize

    // checks if the input can be handled
    if (direction == 1) {   // up arrow key
        if (history_pos == history_count - 1)
            return;

        history_pos++;
    } else if (direction == -1) {   // down arrow key
        if (history_pos == 0) {
            // we are at input 0
            // we want next line after input 0 to be cleared when down arrow is pressed
            history_pos = -1;
            buf_size = 0;
            cursor_pos = 0;
            memset(input, 0, buf_size);

            // clear the line
            _write(0, "\r> ", 3);

            for (int i = 0; i < MAX_INPUT; i++) {
                _write(0, " ", 1); // overwrite the entire line
            }

            for (int i = 0; i < MAX_INPUT; i++) {
                _write(0, "\b", 1); // move cursor back to the beginning
            }

            return;
        }

        if (history_pos == -1) {
            return;
        } else {
            history_pos--;
        }
    }
    
    // If the input is valid (between 0 and MAX_HISTORY), display the command history
    if (history_pos >= 0 && history_pos < history_count) {
        // Set input to the history command
        strncpy(input, history[history_pos], MAX_INPUT);
        buf_size = strlen(input);
        cursor_pos = buf_size;

        // Reprint the line
        _write(0, "\r> ", 3);  // Clear the line and reprint the prompt
        _write(0, input, buf_size);  // Display the history command
        for (int i = buf_size; i < MAX_INPUT; i++) {
            _write(0, " ", 1);  // Clear any leftover characters
        }
        for (int i = buf_size; i < MAX_INPUT; i++) {
            _write(0, "\b", 1);  // Move cursor back to the correct position
        }
    }
}



void save_to_history(const char * input) {
    if (buf_size == 0) return;  // ignore empty inputs

    // shift older inputs up
    for (int i = MAX_HISTORY - 1; i > 0; i--) {
        memset(history[i], 0, MAX_INPUT);   // clear the buffer first
        strncpy(history[i], history[i - 1], MAX_INPUT);
    }

    // place the new input at index 0
    strncpy(history[0], input, MAX_INPUT);
    history[0][buf_size] = '\0';
    
    // update history count if not full yet
    if (history_count < MAX_HISTORY) {
        history_count++;
    }
}



void handle_new_line() {
    // _write(0, "\r\n", sizeof("\r\n"));

    save_to_history(input);
    
    // print new prompt
    _write(0, "> ", sizeof("> "));
}



void handle_backspace_char() {
    cursor_pos--;
    buf_size--;

    // shift character left from cursor position
    for (int i = cursor_pos; i < buf_size + 1; i++) {
        input[i] = input[i + 1];
    }

    // update the screen
    _write(0, "\b", 1);  // move cursor back
    _write(0, &input[cursor_pos], buf_size - cursor_pos);  // reprint the rest
    _write(0, " ", 1);  // clear the last character
    for (int i = cursor_pos; i <= buf_size; i++) {
        _write(0, "\b", 1);  // move cursor back to the original position
    }
}



void tokenize_input(char * input_copy, char *argv[], int *argc) {
    *argc = 0;  // Initialize the argument counter
    int in_token = 0;   // Flag to track if inside a token
    char *ptr = input_copy;  // Pointer to traverse the global input buffer

    while (*ptr != '\0' && *argc < MAX_ARGS) {
        if (*ptr == ' ') {
            // If a space is encountered, terminate the current token
            if (in_token) {
                *ptr = '\0';  // Null-terminate the token
                in_token = 0; // Reset token flag
            }
        } else {
            // If a non-space character is encountered, start a new token
            if (!in_token) {
                argv[*argc] = ptr;  // Store the start of the token
                (*argc)++;          // Increment argument count
                in_token = 1;       // Set token flag
            }
        }
        ptr++;  // Move to the next character
    }

    argv[*argc] = NULL;  // Null-terminate the array of tokens
}



/**
 * this function executes the given command inputted into the command line
 * 
 * @param argc      number of tokenized inputs
 * @param argv      commands themselves
 */
void execute_command(int argc, char *argv[]) {
    // check if there are no arguments present
    if (argc == 0) {
        _write(0, "\r\n", sizeof("\r\n"));
        return;
    }

    int result;

    // commands to include:
    //      list: lists all of the currently loaded user programs
    //      run: runs the specified user program
    //      help: shows the help menu
    //      exit: exits out of terminal
    //      clear: clears terminal
    if (strncmp(argv[0], "list", 4) == 0) {
        // list the user programs loaded into the kernel
        int num_programs;
        _getnumprogs(&num_programs);
        list_programs(num_programs);
    } else if (strcmp(argv[0], "run") == 0) {
        if (argc < 2 || argc > 2) {
            _write(0, "\r\n", 2);
            _write(0, "usage: run <program>\r\n", sizeof("usage: run <program>\r\n"));
            return;
        } else {
            // run the program
            result = run_program(argv[1]);

            if (result < 0) {
                // failed to run the program
                _write(0, "failed to run program", sizeof("failed to run program"));
            }

            _write(0, "\r\n", 2);
        }
    } else if (strcmp(argv[0], "help") == 0) {
        // list commands, what they do, and how they're used
        print_help();
    } else if (strcmp(argv[0], "exit") == 0) {
        // exit out of terminal
        _msgout("exiting shell\n");
        _exit();
    } else if (strcmp(argv[0], "clear") == 0) {
        // clear terminal
        _write(0, "\x1b[2J\x1b[H", 7);  // ANSI escape codes to clear the screen
    } else if (strcmp(argv[0], "ps") == 0) {
        // this command lists the currently running processes
        list_processes();
    } else if (strcmp(argv[0], "signal") == 0) {
        // signals
        if (argc == 3) {
            int pid = parse_int(argv[2]);

            if (pid < 0 || pid > NPROC - 1) {
                _write(0, "\r\n", 2);
                _write(0, "Invalid PID\r\n", sizeof("Invalid PID\r\n"));
            }
            
            result = get_sig_num(argv[1]);

            if (result < 0) {
                return;
            }

            _signal(pid, result);

            return;
        }

        _write(0, "\r\n", 2);
        _write(0, "Usage: signal SIGNAL_NAME PID", sizeof("Usage: signal SIGNAL_NAME PID"));
        _write(0, "\r\n", 2);
    } else {
        _write(0, "\r\n", sizeof("\r\n"));
        _write(0, "unsupported command", sizeof("unsupported command"));
        _write(0, "\r\n", sizeof("\r\n"));
    }
}



void list_programs(int num_programs) {
    char arg[num_programs * FS_NAMELEN];

    _getprognames(&arg);

    char *ptr = arg;

    _write(0, "\r\n", 2);
    for (int i = 0; i < num_programs; i++) {
        size_t length = strlen(ptr);
        _write(0, ptr, length);
        _write(0, "\r\n", 2);
        ptr += FS_NAMELEN;
    }
}



void list_processes() {
    int pids[NPROC];
    char names[NPROC * FS_NAMELEN];

    memset(pids, -1, sizeof(pids));
    _getprocs(pids, names); 

    char * ptr = names;

    _write(0, "\r\n", 2);
    _write(0, "PID   THREAD NAME\r\n", sizeof("PID   THREAD NAME\r\n"));
    for (int i = 0; i < NPROC; i++) {
        if (pids[i] == -1) {
            ptr += FS_NAMELEN;
            continue;
        }

        int print = pids[i] + 48;

        _write(0, &print, sizeof(int));
        _write(0, "     ", 5);
        _write(0, ptr, FS_NAMELEN);
        _write(0, "\r\n", 2);

        ptr += FS_NAMELEN;
    }
}



/**
 * this function gets the signal type from signal name inputted
 * returns -1 if signal doesn't exist
 * 
 * @param   sig_name    name of the signal to execute 
 * 
 * @return              returns -1 if the signal doesn't exist
 */

int get_sig_num(char * sig_name) {
    int result;
    
    if (strcmp(sig_name, "SIGTERM") == 0) {
        result = SIGTERM;
    } else if (strcmp(sig_name, "SIGKILL") == 0) {
        result = SIGKILL;
    } else if (strcmp(sig_name, "SIGINT") == 0) {
        result = SIGINT;
    } else if (strcmp(sig_name, "SIGALRM") == 0) {
        result = SIGALRM;
    } else if (strcmp(sig_name, "SIGSTOP") == 0) {
        result = SIGSTOP;
    } else if (strcmp(sig_name, "SIGCONT") == 0) {
        result = SIGCONT;
    } else if (strcmp(sig_name, "SIGPIPE") == 0) {
        result = SIGPIPE;
    } else if (strcmp(sig_name, "SIGUSR1") == 0) {
        result = SIGUSR1;
    } else if (strcmp(sig_name, "SIGUSR2") == 0) {
        result = SIGUSR2;
    } else {
        result = -1;
    }

    if (result < 0) {
        _write(0, "Signal does not exist", sizeof("Signal does not exist"));
    }

    _write(0, "\r\n", 2);
}



/**
 * this function runs the name of the program inputted
 * 
 * this function assumes each run command comes with a valid user program
 * name, this needs to be fixed after implementing pipes, can skip for now
 * 
 * @param program       name of the program to run
 * 
 * @return              returns 0 on success and negative value on failure
 */
int run_program(char *program) {
    int result;

    device_counter++;

    if (_fork()) {
        // ------------------------
        // parent process
        // ------------------------

        // do nothing
        return 0;
    } else {
        // ------------------------
        // child process
        // ------------------------
        
        // Open ser1 device as fd=0
        char out;
        out = device_counter + 48;
        _msgout(&out);
        result = _devopen(0, "ser", device_counter);

        if (result < 0) {
            _msgout("_devopen failed");
            _exit();
        }

        // exec trek

        result = _fsopen(2, program);

        if (result < 0) {
            _msgout("_fsopen failed");
            _exit();
        }

        _exec(2);
    }

    // this should not execute
    return -2;
}



void print_help() {
    _write(0, "\r\n", sizeof("\r\n"));
    _write(0, "Supported commands:\r\n", sizeof("Supported commands:\r\n"));
    _write(0, " - list: List runnable programs\r\n", sizeof(" - list: List runnable programs\r\n"));
    _write(0, " - run <program>: Run a program\r\n", sizeof(" - run <program>: Run a program\r\n"));
    _write(0, " - clear: Clear the screen\r\n", sizeof(" - clear: Clear the screen\r\n"));
    _write(0, " - exit: Exit the shell\r\n", sizeof(" - exit: Exit the shell\r\n"));
    _write(0, " - help: Display this help message\r\n", sizeof(" - help: Display this help message\r\n"));
    _write(0, " - ps: List currently running processes\r\n", sizeof(" - ps: List currently running processes\r\n"));
}
