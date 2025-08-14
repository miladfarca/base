// Copyright (c) 2025 miladfarca

enum builtins_terminal_chars {
  T_BACKSPACE = 0x8,
  T_RETURN = 0xd,
  T_SPACE = 0x20
};
void builtins_terminal_process_backspace();
void builtins_terminal_process_return();
void builtins_terminal_add_to_buffer(char c);
void builtins_terminal_init_prompt();
void builtins_terminal_print_line(char *string);

void builtins_echo(int argc, char **argv);

void builtins_help(int argc, char **argv);
