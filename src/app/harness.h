/*
 * harness.h - loopback harness detection (Appendix A, ENV-002)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 */

#ifndef HWVAL_HARNESS_H
#define HWVAL_HARNESS_H

int      hwval_harness_count(void);
uint64_t hwval_harness_detected(void); /* bit i = wire i fitted (43 wires >
                                          32: 64-bit, cppcheck shift) */
const struct hwval_wire *hwval_wire_at(int index);
int      hwval_harness_fitted(int index);
void     hwval_harness_report(void);

#endif /* HWVAL_HARNESS_H */