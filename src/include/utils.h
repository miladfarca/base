// Copyright (c) 2025 miladfarca

// libc
void *memset(void *dest, int val, int len);
void *memcpy(void *dest, const void *src, int len);
int strlen(const char *str);
int strcmp(const char *s1, const char *s2);
int itoa(int value, char *sp, int radix);

// misc
void dbg_print(char *string);
void dbg_printi(int input);
void panic(char *string);
