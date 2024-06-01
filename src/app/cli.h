/*
 * cli.h - terminal command interface (section 6)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     CLI-001..CLI-040
 */

#ifndef HWVAL_CLI_H
#define HWVAL_CLI_H

#include <stddef.h>
#include <stdint.h>

/* Body of the CLI task (created by src/rtos.c, FRT-010). Never returns. */
void hwval_cli_task(void *arg);

/* Operator prompt (CLI-014): print question, wait up to timeout_s for a
 * y/n answer. Returns 1 (yes), 0 (no) or -1 (timeout, SKIP NO_OPERATOR). */
int hwval_cli_ask_yn(const char *question, uint32_t timeout_s);

/* Free-format operator input with timeout; returns 0 ok, -1 timeout. */
int hwval_cli_ask_line(const char *question, char *out, size_t len,
                       uint32_t timeout_s);

#endif /* HWVAL_CLI_H */