/*
 * link.h - HWLINK link mode over USART3 (SRS section 6.4)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * SRS:     LINK-001..LINK-012 (GUI CR-01..CR-08)
 */

#ifndef HWVAL_LINK_H
#define HWVAL_LINK_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HWVAL_PROMPT_YES_NO      = 0,
    HWVAL_PROMPT_LINE        = 1,
    HWVAL_PROMPT_BUTTON      = 2,
    HWVAL_PROMPT_EQUIPMENT   = 3,
    HWVAL_PROMPT_DESTRUCTIVE = 4
} hwval_prompt_kind_t;

/* Early boot: read the link-mode backup flag and prepare resources.  Called
 * after USART3 init, before the banner. */
void hwval_link_boot_init(void);

/* True when the previous reset happened while link mode was active. */
int  hwval_link_boot_active(void);

/* CLI task enters this loop after `mode link`; returns when EnterTextMode
 * (or the OPEN-011 text fallback) asks for the text CLI again. */
void hwval_link_run(void);
/* Hand link mode a COBS frame body (without its 0x00 delimiter) that the
 * text CLI received; hwval_link_run() answers it first. */
void hwval_link_prime(const uint8_t *frame, size_t len);

/* Install/remove the reset-surviving link-mode flag (LINK-010). */
void hwval_link_set_boot_flag(int on);

/* Operator prompt from a test (CLI-014, LINK-008/009).  Returns 0 on answer,
 * -1 on timeout/cancel, -2 when link mode is not active. */
int hwval_link_prompt(hwval_prompt_kind_t kind, const char *question,
                      const char *answers, uint32_t timeout_s,
                      char *out, size_t out_len);

int hwval_link_active(void);

/* Suspend unsolicited Log/Progress events while a clock test changes
 * SYSCLK (the UART baud would otherwise be wrong).  Responses still go
 * out, so Abort keeps working. */
void hwval_link_set_quiet(int on);

#endif /* HWVAL_LINK_H */
