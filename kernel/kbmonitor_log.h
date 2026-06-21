/* SPDX-License-Identifier: GPL-2.0 */
#ifndef KBMONITOR_LOG_H
#define KBMONITOR_LOG_H

#include <linux/types.h>

struct class;

int kbmon_log_chrdev_init(struct class *kbmon_class);
void kbmon_log_chrdev_exit(struct class *kbmon_class);
void kbmon_log_reset(void);
void kbmon_log_record(u64 start_jiffies, u64 now, unsigned int code);

#endif
