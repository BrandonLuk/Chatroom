/*
    Handles input to terminal.
    Sets the terminal to non-canonical mode so that each character input from the keyboard is intercepted and handled.
*/

#pragma once

// ANSI color codes
extern char *terminal_colors[];

void init_chat();
char read_char();
void write_to_term(char *msg, int nbytes);
void write_char_to_input_line(char c);
void write_backspace_to_input_line();
void write_int_to_term(int i);
void clear_input_line();