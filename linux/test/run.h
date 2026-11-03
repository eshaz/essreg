/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * es1869test's runner, apart from what the cases use (et.h): the journal
 * and the report files, for main.c.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ET_RUN_H
#define ET_RUN_H

#include "hal.h"

void et_journal_open(const char *path);
int et_journal_restore(struct et_hal *h);
int et_journal_replay(struct et_hal *h, const char *path);

// an earlier run's measurements, from its TSV: their number, or a
// negative errno; and how many of this run's were compared and moved
int et_baseline_load(const char *path);
void et_baseline_counts(int *compared, int *moved);

int et_report_open(const char *dir);
void et_report_sync(void);
void et_report_close(void);

#endif
