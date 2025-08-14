// Copyright (c) 2025 miladfarca

#include "usart.h"

// libc
// https://github.com/gcc-mirror/gcc/blob/master/libgcc/memset.c
void *memset(void *dest, int val, int len) {
  unsigned char *ptr = dest;
  while (len-- > 0)
    *ptr++ = val;
  return dest;
}

// https://github.com/gcc-mirror/gcc/blob/master/libgcc/memcpy.c
void *memcpy(void *dest, const void *src, int len) {
  char *d = dest;
  const char *s = src;
  while (len--)
    *d++ = *s++;
  return dest;
}

// https://github.com/openbsd/src/blob/master/lib/libc/string/strlen.c
int strlen(const char *str) {
  const char *s;
  for (s = str; *s; ++s)
    ;
  return (s - str);
}

// https://github.com/openbsd/src/blob/master/lib/libc/string/strcmp.c
int strcmp(const char *s1, const char *s2) {
  while (*s1 == *s2++)
    if (*s1++ == 0)
      return (0);
  return (*(unsigned char *)s1 - *(unsigned char *)--s2);
}

// https://stackoverflow.com/a/12386915
int itoa(int value, char *sp, int radix) {
  char tmp[16]; // be careful with the length of the buffer
  char *tp = tmp;
  int i;
  unsigned v;

  int sign = (radix == 10 && value < 0);
  if (sign)
    v = -value;
  else
    v = (unsigned)value;

  while (v || tp == tmp) {
    i = v % radix;
    v /= radix;
    if (i < 10)
      *tp++ = i + '0';
    else
      *tp++ = i + 'a' - 10;
  }

  int len = tp - tmp;

  if (sign) {
    *sp++ = '-';
    len++;
  }

  while (tp > tmp)
    *sp++ = *--tp;

  return len;
}

// misc
void dbg_print(char *string) {
#ifdef DEBUG
  while ((*string) != 0) {
    usart1_write(*(string++));
  }
#endif
}

void dbg_printi(int input) {
#ifdef DEBUG
  char buffer[64] = {0};
  itoa(input, buffer, 10);
  dbg_print(buffer);
#endif
}

void panic(char *string) {
  dbg_print("* PANIC *: ");
  dbg_print(string);
  while (1)
    ;
}
