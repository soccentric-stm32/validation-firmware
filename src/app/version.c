/*
 * version.c - build identity banner (GEN-003)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     GEN-003 (version, commit, build date printed on boot and by the
 *          `version` command), string values come from CMake.
 */

#include <stdio.h>
#include "app/version.h"
#include "bsp/uart3.h"

#ifndef HWVAL_VERSION_STR
#define HWVAL_VERSION_STR "unknown"
#endif
#ifndef HWVAL_GIT_COMMIT
#define HWVAL_GIT_COMMIT "unknown"
#endif
#ifndef HWVAL_BUILD_DATE
#define HWVAL_BUILD_DATE "unknown"
#endif
#ifndef HWVAL_SRS_ID
#define HWVAL_SRS_ID "SRS-HWVAL-F767"
#endif

const char *hwval_version_string(void) { return HWVAL_VERSION_STR; }
const char *hwval_git_commit(void)     { return HWVAL_GIT_COMMIT; }
const char *hwval_build_date(void)     { return HWVAL_BUILD_DATE; }
const char *hwval_srs_id(void)         { return HWVAL_SRS_ID; }

void hwval_version_banner(void)
{
    char line[160];

    (void)snprintf(line, sizeof(line),
        "HWVAL v%s (git %s) built %s [%s]\r\n",
        hwval_version_string(), hwval_git_commit(),
        hwval_build_date(), hwval_srs_id());
    hwval_uart3_write(line, 0U); /* 0 = until NUL */
}