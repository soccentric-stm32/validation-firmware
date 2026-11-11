/*
 * version.h - build identity (GEN-003)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 */

#ifndef HWVAL_VERSION_H
#define HWVAL_VERSION_H

#include <stdint.h>

const char *hwval_version_string(void); /* semantic version                */
const char *hwval_git_commit(void);     /* 12-char short hash              */
const char *hwval_build_date(void);     /* build date/time, commit date    */
const char *hwval_srs_id(void);         /* SRS document this builds        */

/* GEN-003: print the banner on boot and reuse for the `version` command. */
void hwval_version_banner(void);

#endif /* HWVAL_VERSION_H */