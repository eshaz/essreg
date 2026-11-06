/*
 * Minimal assertions for the host-side C tests.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

static int check_count, check_failures;

#define CHECK(cond)                                                            \
  do {                                                                         \
    check_count++;                                                             \
    if (!(cond)) {                                                             \
      check_failures++;                                                        \
      printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);          \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    long check_a_ = (long)(a), check_b_ = (long)(b);                           \
    check_count++;                                                             \
    if (check_a_ != check_b_) {                                                \
      check_failures++;                                                        \
      printf("%s:%d: %s == %s failed: %ld != %ld\n", __FILE__, __LINE__, #a,   \
             #b, check_a_, check_b_);                                          \
    }                                                                          \
  } while (0)

#define CHECK_DONE(name)                                                       \
  (printf("%s: %d checks, %d failures\n", name, check_count, check_failures),  \
   check_failures ? 1 : 0)

#endif
