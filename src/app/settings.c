/*
 * settings.c - runtime settings store
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     CLI-012, CLI-013, CLI-014, CLI-032, CLI-034, CLI-035, CLI-036;
 *          GEN-008 (fault action default halt, settable)
 */

#include "app/settings.h"
#include "services/fault.h"

hwval_settings_t g_set = {
    .mode = HWVAL_MODE_INTERACTIVE,
    .format = HWVAL_FMT_TEXT,
    .color = 1,
    .log_level = 2,               /* info */
    .operator_timeout_s = 60U,    /* CLI-014 default */
    .fault_action = (int)HWVAL_FAULT_HALT,
};

const char *hwval_mode_name(hwval_mode_t m)
{
    if (m == HWVAL_MODE_SCRIPT) { return "script"; }
    if (m == HWVAL_MODE_LINK)   { return "link"; }
    return "interactive";
}

const char *hwval_format_name(hwval_format_t f)
{ return (f == HWVAL_FMT_JSON) ? "json" : "text"; }