/*
 * settings.h - runtime settings store (CLI-032, CLI-034..036, SAFE-related)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 */

#ifndef HWVAL_SETTINGS_H
#define HWVAL_SETTINGS_H

#include <stdint.h>

typedef enum { HWVAL_MODE_INTERACTIVE = 0, HWVAL_MODE_SCRIPT = 1,
               HWVAL_MODE_LINK = 2 } hwval_mode_t;
typedef enum { HWVAL_FMT_TEXT = 0, HWVAL_FMT_JSON = 1 }         hwval_format_t;

typedef struct {
    hwval_mode_t  mode;
    hwval_format_t format;
    int           color;          /* CLI-013: 1 = ANSI colours on             */
    int           log_level;      /* 0 error .. 3 debug (CLI-032)             */
    uint32_t      operator_timeout_s; /* CLI-014, default 60 s                */
    int           fault_action;   /* 0 halt, 1 reset (GEN-008 via fault.c)    */
} hwval_settings_t;

extern hwval_settings_t g_set;

const char *hwval_mode_name(hwval_mode_t m);
const char *hwval_format_name(hwval_format_t f);

#endif /* HWVAL_SETTINGS_H */