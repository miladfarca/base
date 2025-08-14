// Copyright (c) 2025 miladfarca

#include "builtins.h"

void builtins_echo(int argc, char **argv) {
  builtins_terminal_print_line(argv[0]);
}
