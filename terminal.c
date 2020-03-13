#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include "terminal.h"

#define MAXDATASIZE 512

struct termios tp, save;

char *terminal_colors[] = {
    "\x1B[0m",  // RESET
    "\x1B[31m", // RED
    "\x1B[32m", // GREEN
    "\x1B[33m", // YELLOW
    "\x1B[34m", // BLUE
    "\x1B[35m", // MAGENTA
    "\x1B[36m", // CYAN
    "\x1B[37m",  // WHITE
};

char temp_char_buf[1];
char chat_buf[MAXDATASIZE];
int chat_buf_len;

static const char clear_line[] = "\33[2K\r";
static const int clear_line_nbytes = 5;

static const char input_line_starter_symbol[] = "> ";

void reset_input_mode()
{
    tcsetattr(STDERR_FILENO, TCSANOW, &save);
}

// Set terminal to non-canonical mode
void set_terminal_noncanon(struct termios* tp, struct termios* save)
{
    tcgetattr(STDIN_FILENO, save);
    atexit(reset_input_mode);

    if(tcgetattr(STDIN_FILENO, tp) == -1)
    {
        perror("tcgetattr");
        exit(1);
    }
    save = tp;
    tp->c_lflag &= ~(ICANON|ECHO);
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, tp) == -1)
    {
        perror("tcsetattr");
        exit(1);
    }   
}

void init_chat()
{
    chat_buf_len = 0;
    set_terminal_noncanon(&tp, &save);
    write(STDOUT_FILENO, input_line_starter_symbol, sizeof(input_line_starter_symbol));
}

void write_char_to_input_line(char c)
{
    write(STDOUT_FILENO, &c, 1);
}

void write_backspace_to_input_line()
{
    write_char_to_input_line('\b');
    write_char_to_input_line(' ');
    write_char_to_input_line('\b');
}

void clear_input_line()
{
    write(STDOUT_FILENO, clear_line, clear_line_nbytes);
}

char read_char()
{
    read(STDIN_FILENO, temp_char_buf, 1);
    return temp_char_buf[0];
}

// Write an integer to the terminal
void write_int_to_term(int i)
{
    char *buf = malloc(MAXDATASIZE);
    sprintf(buf, "%d\n", i);
    write_chat(buf, strlen(buf));
    free(buf);
}

void write_to_term(char *msg, int nbytes)
{
    clear_input_line();
    write(STDOUT_FILENO, msg, nbytes);
    clear_input_line();
    write(STDOUT_FILENO, input_line_starter_symbol, sizeof(input_line_starter_symbol));
    write(STDOUT_FILENO, chat_buf, chat_buf_len);
}