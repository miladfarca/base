// Copyright (c) 2025 miladfarca

#include "builtins.h"
#include "usb.h"
#include "utils.h"
#include "version.h"
#include <stddef.h>

#define TERMINAL_MAX_CHAR_LENGTH 64
#define TERMINAL_MAX_ARG_COUNT 3
char terminal_buffer[TERMINAL_MAX_CHAR_LENGTH];
int terminal_buffer_index = 0;

static void print_and_clear_buffer(char *string) {
  const char new_line_string[] = "\n\r";
  const char prompt_string[] = "# ";
  memset(terminal_buffer, 0, TERMINAL_MAX_CHAR_LENGTH);
  terminal_buffer_index = 0;
  // add a new line for response
  if (string) {
    for (int i = 0; i < sizeof(new_line_string) - 1; i++) {
      terminal_buffer[terminal_buffer_index++] = new_line_string[i];
    }
  }
  // string input
  if (string) {
    while ((*string) != 0) {
      terminal_buffer[terminal_buffer_index++] = *(string++);
    }
  }
  // new line prompt
  for (int i = 0; i < sizeof(new_line_string) - 1; i++) {
    terminal_buffer[terminal_buffer_index++] = new_line_string[i];
  }
  for (int i = 0; i < sizeof(prompt_string) - 1; i++) {
    terminal_buffer[terminal_buffer_index++] = prompt_string[i];
  }
  usb_terminal_print(terminal_buffer);
  memset(terminal_buffer, 0, TERMINAL_MAX_CHAR_LENGTH);
  terminal_buffer_index = 0;
}

void builtins_terminal_process_backspace() {
  if (terminal_buffer_index > 0) {
    terminal_buffer[terminal_buffer_index--] = 0;
    usb_terminal_print("\x1B[D \x1B[D");
  }
}

void builtins_terminal_process_return() {
  if (terminal_buffer_index > 0) {
    char program[TERMINAL_MAX_CHAR_LENGTH] = {0};
    char arg0[TERMINAL_MAX_CHAR_LENGTH] = {0};
    char arg1[TERMINAL_MAX_CHAR_LENGTH] = {0};
    char arg2[TERMINAL_MAX_CHAR_LENGTH] = {0};
    char *argv[TERMINAL_MAX_ARG_COUNT] = {arg0, arg1, arg2};
    // get the program
    int index = 0;
    for (int i = 0; index < terminal_buffer_index; index++, i++) {
      if (terminal_buffer[index] == 0) {
        break;
      } else if (terminal_buffer[index] == T_SPACE) {
        index++;
        break;
      }
      program[i] = terminal_buffer[index];
    }
    // get the args
    int arg_index = 0;
    for (int i = 0; index < terminal_buffer_index; index++, i++) {
      if (terminal_buffer[index] == 0 || arg_index >= TERMINAL_MAX_ARG_COUNT) {
        break;
      } else if (terminal_buffer[index] == T_SPACE) {
        index++;
        arg_index++;
        break;
      }
      argv[arg_index][i] = terminal_buffer[index];
    }

    if (strcmp(program, "help") == 0) {
      builtins_help(TERMINAL_MAX_ARG_COUNT, argv);
      return;
    } else if (strcmp(program, "echo") == 0) {
      builtins_echo(TERMINAL_MAX_ARG_COUNT, argv);
      return;
    } else {
      // not a valid command
      print_and_clear_buffer("Command not found");
      return;
    }
  }

  print_and_clear_buffer(NULL);
}

void builtins_terminal_add_to_buffer(char c) {
  if (terminal_buffer_index < TERMINAL_MAX_CHAR_LENGTH) {
    terminal_buffer[terminal_buffer_index++] = c;
  }
}

void builtins_terminal_init_prompt() {
  print_and_clear_buffer("* Base - version " VERSION
                         " *\n\rType help for available commands.");
}

void builtins_terminal_print_line(char *string) {
  print_and_clear_buffer(string);
}
