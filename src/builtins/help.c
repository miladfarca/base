// Copyright (c) 2025 miladfarca

#include "builtins.h"

void builtins_help(int argc, char **argv) {
  char *string = "List of commands: help echo";
  builtins_terminal_print_line(string);
}
