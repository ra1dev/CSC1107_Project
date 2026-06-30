// SPDX-License-Identifier: GPL-2.0
/*
 * kbmonitor_log - bounded Linux key-name event log for kbmonitor.
 *
 * This file keeps event-by-event keypress logging separate from the aggregate
 * statistics exposed by /dev/kbmonitor.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "kbmonitor_log.h"

#define KBMON_LOG_DEVICE_NAME "kbmonitor_log"
#define KBMON_LOG_EVENTS      128
#define KBMON_LOG_OUT_SIZE    16384

/*
 * /dev/kbmonitor_log stores a bounded sequence of recent key names for local
 * demonstration.  It is kept separate from /dev/kbmonitor so aggregate
 * statistics and event-by-event log output can be described and tested
 * independently.
 */
struct kbmon_log_event {
	u64 seq;
	u64 time_ms;
	unsigned int code;
};

struct kbmon_log_state {
	spinlock_t lock;
	struct kbmon_log_event events[KBMON_LOG_EVENTS];
	u64 next_seq;
	u64 dropped;
	unsigned int head;
	unsigned int count;
};

static dev_t kbmon_log_devno;
static struct cdev kbmon_log_cdev;
static struct device *kbmon_log_device;
static struct kbmon_log_state kbmon_log;

#define KBMON_KEY_NAME(name) case KEY_##name: return "KEY_" #name

/*
 * Convert common Linux input key codes into stable names for the log output.
 * Unknown keys are still printed by numeric code so no event is silently lost.
 */
static const char *kbmon_log_key_name(unsigned int code)
{
	switch (code) {
	KBMON_KEY_NAME(RESERVED);
	KBMON_KEY_NAME(ESC);
	case KEY_0:
		return "KEY_0";
	case KEY_1:
		return "KEY_1";
	case KEY_2:
		return "KEY_2";
	case KEY_3:
		return "KEY_3";
	case KEY_4:
		return "KEY_4";
	case KEY_5:
		return "KEY_5";
	case KEY_6:
		return "KEY_6";
	case KEY_7:
		return "KEY_7";
	case KEY_8:
		return "KEY_8";
	case KEY_9:
		return "KEY_9";
	KBMON_KEY_NAME(MINUS);
	KBMON_KEY_NAME(EQUAL);
	KBMON_KEY_NAME(BACKSPACE);
	KBMON_KEY_NAME(TAB);
	KBMON_KEY_NAME(Q);
	KBMON_KEY_NAME(W);
	KBMON_KEY_NAME(E);
	KBMON_KEY_NAME(R);
	KBMON_KEY_NAME(T);
	KBMON_KEY_NAME(Y);
	KBMON_KEY_NAME(U);
	KBMON_KEY_NAME(I);
	KBMON_KEY_NAME(O);
	KBMON_KEY_NAME(P);
	KBMON_KEY_NAME(LEFTBRACE);
	KBMON_KEY_NAME(RIGHTBRACE);
	KBMON_KEY_NAME(ENTER);
	KBMON_KEY_NAME(LEFTCTRL);
	KBMON_KEY_NAME(A);
	KBMON_KEY_NAME(S);
	KBMON_KEY_NAME(D);
	KBMON_KEY_NAME(F);
	KBMON_KEY_NAME(G);
	KBMON_KEY_NAME(H);
	KBMON_KEY_NAME(J);
	KBMON_KEY_NAME(K);
	KBMON_KEY_NAME(L);
	KBMON_KEY_NAME(SEMICOLON);
	KBMON_KEY_NAME(APOSTROPHE);
	KBMON_KEY_NAME(GRAVE);
	KBMON_KEY_NAME(LEFTSHIFT);
	KBMON_KEY_NAME(BACKSLASH);
	KBMON_KEY_NAME(Z);
	KBMON_KEY_NAME(X);
	KBMON_KEY_NAME(C);
	KBMON_KEY_NAME(V);
	KBMON_KEY_NAME(B);
	KBMON_KEY_NAME(N);
	KBMON_KEY_NAME(M);
	KBMON_KEY_NAME(COMMA);
	KBMON_KEY_NAME(DOT);
	KBMON_KEY_NAME(SLASH);
	KBMON_KEY_NAME(RIGHTSHIFT);
	KBMON_KEY_NAME(KPASTERISK);
	KBMON_KEY_NAME(LEFTALT);
	KBMON_KEY_NAME(SPACE);
	KBMON_KEY_NAME(CAPSLOCK);
	KBMON_KEY_NAME(F1);
	KBMON_KEY_NAME(F2);
	KBMON_KEY_NAME(F3);
	KBMON_KEY_NAME(F4);
	KBMON_KEY_NAME(F5);
	KBMON_KEY_NAME(F6);
	KBMON_KEY_NAME(F7);
	KBMON_KEY_NAME(F8);
	KBMON_KEY_NAME(F9);
	KBMON_KEY_NAME(F10);
	KBMON_KEY_NAME(F11);
	KBMON_KEY_NAME(F12);
	KBMON_KEY_NAME(NUMLOCK);
	KBMON_KEY_NAME(SCROLLLOCK);
	KBMON_KEY_NAME(KP7);
	KBMON_KEY_NAME(KP8);
	KBMON_KEY_NAME(KP9);
	KBMON_KEY_NAME(KPMINUS);
	KBMON_KEY_NAME(KP4);
	KBMON_KEY_NAME(KP5);
	KBMON_KEY_NAME(KP6);
	KBMON_KEY_NAME(KPPLUS);
	KBMON_KEY_NAME(KP1);
	KBMON_KEY_NAME(KP2);
	KBMON_KEY_NAME(KP3);
	KBMON_KEY_NAME(KP0);
	KBMON_KEY_NAME(KPDOT);
	KBMON_KEY_NAME(KPENTER);
	KBMON_KEY_NAME(RIGHTCTRL);
	KBMON_KEY_NAME(KPSLASH);
	KBMON_KEY_NAME(SYSRQ);
	KBMON_KEY_NAME(RIGHTALT);
	KBMON_KEY_NAME(LINEFEED);
	KBMON_KEY_NAME(HOME);
	KBMON_KEY_NAME(UP);
	KBMON_KEY_NAME(PAGEUP);
	KBMON_KEY_NAME(LEFT);
	KBMON_KEY_NAME(RIGHT);
	KBMON_KEY_NAME(END);
	KBMON_KEY_NAME(DOWN);
	KBMON_KEY_NAME(PAGEDOWN);
	KBMON_KEY_NAME(INSERT);
	KBMON_KEY_NAME(DELETE);
	KBMON_KEY_NAME(MUTE);
	KBMON_KEY_NAME(VOLUMEDOWN);
	KBMON_KEY_NAME(VOLUMEUP);
	KBMON_KEY_NAME(POWER);
	KBMON_KEY_NAME(KPEQUAL);
	KBMON_KEY_NAME(PAUSE);
	KBMON_KEY_NAME(LEFTMETA);
	KBMON_KEY_NAME(RIGHTMETA);
	KBMON_KEY_NAME(COMPOSE);
	case KEY_102ND:
		return "KEY_102ND";
	default:
		return NULL;
	}
}

static int kbmon_log_append(char *out, int size, int len, const char *fmt, ...)
{
	va_list args;
	int written;

	if (len >= size)
		return len;

	va_start(args, fmt);
	written = vscnprintf(out + len, size - len, fmt, args);
	va_end(args);

	return len + written;
}

void kbmon_log_reset(void)
{
	unsigned long flags;

	/* Reset the bounded log while holding the same lock used by writers. */
	spin_lock_irqsave(&kbmon_log.lock, flags);
	kbmon_log.next_seq = 0;
	kbmon_log.dropped = 0;
	kbmon_log.head = 0;
	kbmon_log.count = 0;
	memset(kbmon_log.events, 0, sizeof(kbmon_log.events));
	spin_unlock_irqrestore(&kbmon_log.lock, flags);
}

void kbmon_log_record(u64 start_jiffies, u64 now, unsigned int code)
{
	struct kbmon_log_event *event;
	unsigned long flags;

	/*
	 * The log is a ring buffer.  When it fills, new events replace the oldest
	 * entries and dropped records how many historical entries were overwritten.
	 */
	spin_lock_irqsave(&kbmon_log.lock, flags);
	event = &kbmon_log.events[kbmon_log.head];
	event->seq = kbmon_log.next_seq++;
	event->time_ms = jiffies64_to_msecs(now - start_jiffies);
	event->code = code;

	kbmon_log.head = (kbmon_log.head + 1) % KBMON_LOG_EVENTS;
	if (kbmon_log.count < KBMON_LOG_EVENTS)
		kbmon_log.count++;
	else
		kbmon_log.dropped++;
	spin_unlock_irqrestore(&kbmon_log.lock, flags);
}

static int kbmon_log_format(char *out, int size)
{
	struct kbmon_log_event *events;
	unsigned long flags;
	u64 dropped;
	unsigned int count;
	unsigned int start;
	unsigned int i;
	int len;

	events = kcalloc(KBMON_LOG_EVENTS, sizeof(*events), GFP_KERNEL);
	if (!events)
		return scnprintf(out, size, "error=ENOMEM\n");

	/*
	 * Copy log entries out while locked, then format after unlocking.  This
	 * avoids holding a spinlock during the slower string-building loop.
	 */
	spin_lock_irqsave(&kbmon_log.lock, flags);
	count = kbmon_log.count;
	dropped = kbmon_log.dropped;
	start = (kbmon_log.head + KBMON_LOG_EVENTS - count) % KBMON_LOG_EVENTS;
	for (i = 0; i < count; i++)
		events[i] = kbmon_log.events[(start + i) % KBMON_LOG_EVENTS];
	spin_unlock_irqrestore(&kbmon_log.lock, flags);

	len = scnprintf(out, size,
			"driver=kbmonitor\n"
			"view=log\n"
			"events=%u\n"
			"log_capacity=%u\n"
			"log_dropped=%llu\n"
			"log_begin\n",
			count, KBMON_LOG_EVENTS, (unsigned long long)dropped);

	for (i = 0; i < count; i++) {
		const char *name = kbmon_log_key_name(events[i].code);

		if (name) {
			len = kbmon_log_append(out, size, len,
					       "seq=%llu time_ms=%llu code=%u key=%s\n",
					       (unsigned long long)events[i].seq,
					       (unsigned long long)events[i].time_ms,
					       events[i].code, name);
		} else {
			len = kbmon_log_append(out, size, len,
					       "seq=%llu time_ms=%llu code=%u key=KEY_%u\n",
					       (unsigned long long)events[i].seq,
					       (unsigned long long)events[i].time_ms,
					       events[i].code, events[i].code);
		}
	}

	len = kbmon_log_append(out, size, len, "log_end\n");
	kfree(events);
	return len;
}

static ssize_t kbmon_log_read(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	char *out;
	int len;
	ssize_t ret;

	out = kzalloc(KBMON_LOG_OUT_SIZE, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	/* simple_read_from_buffer gives normal file-like read behaviour. */
	len = kbmon_log_format(out, KBMON_LOG_OUT_SIZE);
	ret = simple_read_from_buffer(user_buf, count, ppos, out, len);
	kfree(out);
	return ret;
}

static int kbmon_log_open(struct inode *inode, struct file *file)
{
	pr_info("kbmonitor: /dev/%s opened\n", KBMON_LOG_DEVICE_NAME);
	return 0;
}

static int kbmon_log_release(struct inode *inode, struct file *file)
{
	pr_info("kbmonitor: /dev/%s closed\n", KBMON_LOG_DEVICE_NAME);
	return 0;
}

static const struct file_operations kbmon_log_fops = {
	/* /dev/kbmonitor_log is read-only from user space. */
	.owner = THIS_MODULE,
	.open = kbmon_log_open,
	.read = kbmon_log_read,
	.release = kbmon_log_release,
	.llseek = noop_llseek,
};

int kbmon_log_chrdev_init(struct class *kbmon_class)
{
	int err;

	/*
	 * Reuse the main kbmonitor class so both device nodes appear together
	 * under /dev, but allocate a separate device number for the log.
	 */
	spin_lock_init(&kbmon_log.lock);
	kbmon_log_reset();

	err = alloc_chrdev_region(&kbmon_log_devno, 0, 1,
				  KBMON_LOG_DEVICE_NAME);
	if (err)
		return err;

	cdev_init(&kbmon_log_cdev, &kbmon_log_fops);
	kbmon_log_cdev.owner = THIS_MODULE;

	err = cdev_add(&kbmon_log_cdev, kbmon_log_devno, 1);
	if (err)
		goto err_unregister_region;

	kbmon_log_device = device_create(kbmon_class, NULL, kbmon_log_devno,
					 NULL, KBMON_LOG_DEVICE_NAME);
	if (IS_ERR(kbmon_log_device)) {
		err = PTR_ERR(kbmon_log_device);
		goto err_cdev_del;
	}

	pr_info("kbmonitor: created /dev/%s major=%d minor=%d\n",
		KBMON_LOG_DEVICE_NAME, MAJOR(kbmon_log_devno),
		MINOR(kbmon_log_devno));
	return 0;

err_cdev_del:
	cdev_del(&kbmon_log_cdev);
err_unregister_region:
	unregister_chrdev_region(kbmon_log_devno, 1);
	return err;
}

void kbmon_log_chrdev_exit(struct class *kbmon_class)
{
	/* Called by the main module cleanup path after input callbacks stop. */
	device_destroy(kbmon_class, kbmon_log_devno);
	cdev_del(&kbmon_log_cdev);
	unregister_chrdev_region(kbmon_log_devno, 1);
	pr_info("kbmonitor: removed /dev/%s\n", KBMON_LOG_DEVICE_NAME);
}
