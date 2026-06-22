// SPDX-License-Identifier: GPL-2.0
/*
 * kbmonitor - USB keyboard activity monitor for CSC1107 Project 2.
 *
 * This module does not replace the normal Linux USB keyboard driver.
 * It passively observes keyboard events from the input subsystem and exposes
 * aggregate activity statistics through /dev/kbmonitor.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "kbmonitor_log.h"

#define KBMON_DEVICE_NAME "kbmonitor"
#define KBMON_CLASS_NAME  "kbmonitor"
#define KBMON_MAX_CMD     64
#define KBMON_OUT_SIZE    16384
#define KBMON_EVENT_BUF   64
#define KBMON_KEY_COUNT   (KEY_MAX + 1)
#define KBMON_TOP_KEYS    5
#define KBMON_TEXT_BUF    512

enum kbmon_view {
	KBMON_VIEW_SUMMARY = 0,
	KBMON_VIEW_KEYS,
	KBMON_VIEW_EVENTS,
	KBMON_VIEW_STATUS,
	KBMON_VIEW_HELP,
	KBMON_VIEW_TEXT,
};

struct kbmon_state {
	spinlock_t lock;
	u64 total_presses;
	u64 repeat_events;
	u64 start_jiffies;
	u64 last_press_jiffies;
	u64 buffer_dropped;
	u64 event_times[KBMON_EVENT_BUF];
	unsigned int event_codes[KBMON_EVENT_BUF];
	u64 key_counts[KBMON_KEY_COUNT];
	unsigned int event_head;
	unsigned int event_count;
	enum kbmon_view view;
	atomic_t active_keyboards;
#ifdef ENABLE_TEXT_MODE
	bool text_mode_enabled;
	bool shift_down;
	bool caps_lock;
	char text_buf[KBMON_TEXT_BUF];
	unsigned int text_len;
	u64 text_dropped;
#endif
};

struct kbmon_snapshot {
	u64 total_presses;
	u64 repeat_events;
	u64 uptime_ms;
	u64 last_press_ms;
	u64 presses_per_minute;
	u64 presses_last_10s;
	u64 peak_presses_per_second;
	u64 buffer_dropped;
	unsigned int buffered_events;
	enum kbmon_view view;
	int active_keyboards;
};

struct kbmon_events_snapshot {
	u64 times[KBMON_EVENT_BUF];
	unsigned int codes[KBMON_EVENT_BUF];
	unsigned int count;
	u64 start_jiffies;
	u64 now_jiffies;
};

struct kbmon_input_handle {
	struct input_handle handle;
};

static dev_t kbmon_devno;
static struct cdev kbmon_cdev;
static struct class *kbmon_class;
static struct device *kbmon_device;
static struct kbmon_state kbmon;

static bool allow_non_usb;
module_param(allow_non_usb, bool, 0444);
MODULE_PARM_DESC(allow_non_usb,
		 "Allow non-USB keyboard-like devices for development/testing");

static bool kbmon_is_letter_key(unsigned int code)
{
	switch (code) {
	case KEY_A:
	case KEY_B:
	case KEY_C:
	case KEY_D:
	case KEY_E:
	case KEY_F:
	case KEY_G:
	case KEY_H:
	case KEY_I:
	case KEY_J:
	case KEY_K:
	case KEY_L:
	case KEY_M:
	case KEY_N:
	case KEY_O:
	case KEY_P:
	case KEY_Q:
	case KEY_R:
	case KEY_S:
	case KEY_T:
	case KEY_U:
	case KEY_V:
	case KEY_W:
	case KEY_X:
	case KEY_Y:
	case KEY_Z:
		return true;
	default:
		return false;
	}
}

static bool kbmon_is_digit_key(unsigned int code)
{
	switch (code) {
	case KEY_0:
	case KEY_1:
	case KEY_2:
	case KEY_3:
	case KEY_4:
	case KEY_5:
	case KEY_6:
	case KEY_7:
	case KEY_8:
	case KEY_9:
		return true;
	default:
		return false;
	}
}

static bool kbmon_is_modifier_key(unsigned int code)
{
	switch (code) {
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:
	case KEY_LEFTSHIFT:
	case KEY_RIGHTSHIFT:
	case KEY_LEFTALT:
	case KEY_RIGHTALT:
	case KEY_LEFTMETA:
	case KEY_RIGHTMETA:
	case KEY_CAPSLOCK:
		return true;
	default:
		return false;
	}
}

static bool kbmon_is_navigation_key(unsigned int code)
{
	switch (code) {
	case KEY_UP:
	case KEY_DOWN:
	case KEY_LEFT:
	case KEY_RIGHT:
	case KEY_HOME:
	case KEY_END:
	case KEY_PAGEUP:
	case KEY_PAGEDOWN:
	case KEY_INSERT:
	case KEY_DELETE:
		return true;
	default:
		return false;
	}
}

static bool kbmon_is_function_key(unsigned int code)
{
	return (code >= KEY_F1 && code <= KEY_F10) ||
	       code == KEY_F11 || code == KEY_F12 ||
	       (code >= KEY_F13 && code <= KEY_F24);
}

static bool kbmon_is_control_key(unsigned int code)
{
	switch (code) {
	case KEY_ESC:
	case KEY_BACKSPACE:
	case KEY_TAB:
	case KEY_ENTER:
	case KEY_SPACE:
		return true;
	default:
		return false;
	}
}

static const char *kbmon_key_label(unsigned int code)
{
	switch (code) {
	case KEY_ESC:
		return "ESC";
	case KEY_1:
		return "1";
	case KEY_2:
		return "2";
	case KEY_3:
		return "3";
	case KEY_4:
		return "4";
	case KEY_5:
		return "5";
	case KEY_6:
		return "6";
	case KEY_7:
		return "7";
	case KEY_8:
		return "8";
	case KEY_9:
		return "9";
	case KEY_0:
		return "0";
	case KEY_MINUS:
		return "MINUS";
	case KEY_EQUAL:
		return "EQUAL";
	case KEY_BACKSPACE:
		return "BACKSPACE";
	case KEY_TAB:
		return "TAB";
	case KEY_Q:
		return "Q";
	case KEY_W:
		return "W";
	case KEY_E:
		return "E";
	case KEY_R:
		return "R";
	case KEY_T:
		return "T";
	case KEY_Y:
		return "Y";
	case KEY_U:
		return "U";
	case KEY_I:
		return "I";
	case KEY_O:
		return "O";
	case KEY_P:
		return "P";
	case KEY_LEFTBRACE:
		return "LEFTBRACE";
	case KEY_RIGHTBRACE:
		return "RIGHTBRACE";
	case KEY_ENTER:
		return "ENTER";
	case KEY_LEFTCTRL:
		return "LEFTCTRL";
	case KEY_A:
		return "A";
	case KEY_S:
		return "S";
	case KEY_D:
		return "D";
	case KEY_F:
		return "F";
	case KEY_G:
		return "G";
	case KEY_H:
		return "H";
	case KEY_J:
		return "J";
	case KEY_K:
		return "K";
	case KEY_L:
		return "L";
	case KEY_SEMICOLON:
		return "SEMICOLON";
	case KEY_APOSTROPHE:
		return "APOSTROPHE";
	case KEY_GRAVE:
		return "GRAVE";
	case KEY_LEFTSHIFT:
		return "LEFTSHIFT";
	case KEY_BACKSLASH:
		return "BACKSLASH";
	case KEY_Z:
		return "Z";
	case KEY_X:
		return "X";
	case KEY_C:
		return "C";
	case KEY_V:
		return "V";
	case KEY_B:
		return "B";
	case KEY_N:
		return "N";
	case KEY_M:
		return "M";
	case KEY_COMMA:
		return "COMMA";
	case KEY_DOT:
		return "DOT";
	case KEY_SLASH:
		return "SLASH";
	case KEY_RIGHTSHIFT:
		return "RIGHTSHIFT";
	case KEY_LEFTALT:
		return "LEFTALT";
	case KEY_SPACE:
		return "SPACE";
	case KEY_CAPSLOCK:
		return "CAPSLOCK";
	case KEY_F1:
		return "F1";
	case KEY_F2:
		return "F2";
	case KEY_F3:
		return "F3";
	case KEY_F4:
		return "F4";
	case KEY_F5:
		return "F5";
	case KEY_F6:
		return "F6";
	case KEY_F7:
		return "F7";
	case KEY_F8:
		return "F8";
	case KEY_F9:
		return "F9";
	case KEY_F10:
		return "F10";
	case KEY_F11:
		return "F11";
	case KEY_F12:
		return "F12";
	case KEY_RIGHTCTRL:
		return "RIGHTCTRL";
	case KEY_RIGHTALT:
		return "RIGHTALT";
	case KEY_HOME:
		return "HOME";
	case KEY_UP:
		return "UP";
	case KEY_PAGEUP:
		return "PAGEUP";
	case KEY_LEFT:
		return "LEFT";
	case KEY_RIGHT:
		return "RIGHT";
	case KEY_END:
		return "END";
	case KEY_DOWN:
		return "DOWN";
	case KEY_PAGEDOWN:
		return "PAGEDOWN";
	case KEY_INSERT:
		return "INSERT";
	case KEY_DELETE:
		return "DELETE";
	case KEY_LEFTMETA:
		return "LEFTMETA";
	case KEY_RIGHTMETA:
		return "RIGHTMETA";
	default:
		return "UNKNOWN";
	}
}

#ifdef ENABLE_TEXT_MODE
static void kbmon_text_clear_locked(void)
{
	memset(kbmon.text_buf, 0, sizeof(kbmon.text_buf));
	kbmon.text_len = 0;
	kbmon.text_dropped = 0;
}

static void kbmon_text_append_locked(char ch)
{
	if (kbmon.text_len + 1 >= KBMON_TEXT_BUF) {
		kbmon.text_dropped++;
		return;
	}

	kbmon.text_buf[kbmon.text_len++] = ch;
	kbmon.text_buf[kbmon.text_len] = '\0';
}

static void kbmon_text_backspace_locked(void)
{
	if (kbmon.text_len == 0)
		return;

	kbmon.text_len--;
	kbmon.text_buf[kbmon.text_len] = '\0';
}

static char kbmon_letter_char(unsigned int code, bool uppercase)
{
	char ch;

	switch (code) {
	case KEY_A:
		ch = 'a';
		break;
	case KEY_B:
		ch = 'b';
		break;
	case KEY_C:
		ch = 'c';
		break;
	case KEY_D:
		ch = 'd';
		break;
	case KEY_E:
		ch = 'e';
		break;
	case KEY_F:
		ch = 'f';
		break;
	case KEY_G:
		ch = 'g';
		break;
	case KEY_H:
		ch = 'h';
		break;
	case KEY_I:
		ch = 'i';
		break;
	case KEY_J:
		ch = 'j';
		break;
	case KEY_K:
		ch = 'k';
		break;
	case KEY_L:
		ch = 'l';
		break;
	case KEY_M:
		ch = 'm';
		break;
	case KEY_N:
		ch = 'n';
		break;
	case KEY_O:
		ch = 'o';
		break;
	case KEY_P:
		ch = 'p';
		break;
	case KEY_Q:
		ch = 'q';
		break;
	case KEY_R:
		ch = 'r';
		break;
	case KEY_S:
		ch = 's';
		break;
	case KEY_T:
		ch = 't';
		break;
	case KEY_U:
		ch = 'u';
		break;
	case KEY_V:
		ch = 'v';
		break;
	case KEY_W:
		ch = 'w';
		break;
	case KEY_X:
		ch = 'x';
		break;
	case KEY_Y:
		ch = 'y';
		break;
	case KEY_Z:
		ch = 'z';
		break;
	default:
		return '\0';
	}

	if (uppercase)
		ch -= 'a' - 'A';

	return ch;
}

static char kbmon_digit_char(unsigned int code, bool shifted)
{
	switch (code) {
	case KEY_0:
		return shifted ? ')' : '0';
	case KEY_1:
		return shifted ? '!' : '1';
	case KEY_2:
		return shifted ? '@' : '2';
	case KEY_3:
		return shifted ? '#' : '3';
	case KEY_4:
		return shifted ? '$' : '4';
	case KEY_5:
		return shifted ? '%' : '5';
	case KEY_6:
		return shifted ? '^' : '6';
	case KEY_7:
		return shifted ? '&' : '7';
	case KEY_8:
		return shifted ? '*' : '8';
	case KEY_9:
		return shifted ? '(' : '9';
	default:
		return '\0';
	}
}

static char kbmon_symbol_char(unsigned int code, bool shifted)
{
	switch (code) {
	case KEY_MINUS:
		return shifted ? '_' : '-';
	case KEY_EQUAL:
		return shifted ? '+' : '=';
	case KEY_LEFTBRACE:
		return shifted ? '{' : '[';
	case KEY_RIGHTBRACE:
		return shifted ? '}' : ']';
	case KEY_BACKSLASH:
		return shifted ? '|' : '\\';
	case KEY_SEMICOLON:
		return shifted ? ':' : ';';
	case KEY_APOSTROPHE:
		return shifted ? '"' : '\'';
	case KEY_GRAVE:
		return shifted ? '~' : '`';
	case KEY_COMMA:
		return shifted ? '<' : ',';
	case KEY_DOT:
		return shifted ? '>' : '.';
	case KEY_SLASH:
		return shifted ? '?' : '/';
	default:
		return '\0';
	}
}

static char kbmon_text_char(unsigned int code)
{
	char ch;
	bool uppercase = kbmon.shift_down ^ kbmon.caps_lock;

	ch = kbmon_letter_char(code, uppercase);
	if (ch)
		return ch;

	ch = kbmon_digit_char(code, kbmon.shift_down);
	if (ch)
		return ch;

	return kbmon_symbol_char(code, kbmon.shift_down);
}

static void kbmon_text_record_locked(unsigned int code, int value)
{
	char ch;

	if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
		kbmon.shift_down = value != 0;
		return;
	}

	if (value != 1)
		return;

	if (code == KEY_CAPSLOCK) {
		kbmon.caps_lock = !kbmon.caps_lock;
		return;
	}

	if (!kbmon.text_mode_enabled)
		return;

	switch (code) {
	case KEY_BACKSPACE:
		kbmon_text_backspace_locked();
		return;
	case KEY_ENTER:
		kbmon_text_append_locked('\n');
		return;
	case KEY_SPACE:
		kbmon_text_append_locked(' ');
		return;
	default:
		break;
	}

	ch = kbmon_text_char(code);
	if (ch)
		kbmon_text_append_locked(ch);
}

static int kbmon_format_text(char *out, int size)
{
	unsigned long flags;
	int len;

	spin_lock_irqsave(&kbmon.lock, flags);
	len = scnprintf(out, size,
			"driver=kbmonitor\n"
			"view=text\n"
			"text_mode=enabled\n"
			"text_len=%u\n"
			"text_dropped=%llu\n"
			"text_begin\n"
			"%s\n"
			"text_end\n",
			kbmon.text_len,
			(unsigned long long)kbmon.text_dropped,
			kbmon.text_buf);
	spin_unlock_irqrestore(&kbmon.lock, flags);

	return len;
}
#endif

static void kbmon_record_press_locked(u64 now, unsigned int code)
{
	kbmon.total_presses++;
	kbmon.last_press_jiffies = now;
	if (code <= KEY_MAX)
		kbmon.key_counts[code]++;

	kbmon.event_times[kbmon.event_head] = now;
	kbmon.event_codes[kbmon.event_head] = code;
	kbmon.event_head = (kbmon.event_head + 1) % KBMON_EVENT_BUF;
	if (kbmon.event_count < KBMON_EVENT_BUF)
		kbmon.event_count++;
	else
		kbmon.buffer_dropped++;
}

static void kbmon_reset_locked(u64 now)
{
	kbmon.total_presses = 0;
	kbmon.repeat_events = 0;
	kbmon.start_jiffies = now;
	kbmon.last_press_jiffies = 0;
	kbmon.buffer_dropped = 0;
	kbmon.event_head = 0;
	kbmon.event_count = 0;
	memset(kbmon.event_times, 0, sizeof(kbmon.event_times));
	memset(kbmon.event_codes, 0, sizeof(kbmon.event_codes));
	memset(kbmon.key_counts, 0, sizeof(kbmon.key_counts));
	kbmon.view = KBMON_VIEW_SUMMARY;
#ifdef ENABLE_TEXT_MODE
	kbmon.text_mode_enabled = false;
	kbmon.shift_down = false;
	kbmon.caps_lock = false;
	kbmon_text_clear_locked();
#endif
}

static void kbmon_snapshot_stats(struct kbmon_snapshot *snap)
{
	unsigned long flags;
	u64 now = get_jiffies_64();
	u64 start;
	u64 last;
	u64 ten_seconds = msecs_to_jiffies(10000);
	u64 cutoff_10s = now > ten_seconds ? now - ten_seconds : 0;
	u64 one_second = msecs_to_jiffies(1000);
	u64 event_times[KBMON_EVENT_BUF];
	unsigned int event_count;
	unsigned int i;
	unsigned int j;
	u64 peak = 0;

	spin_lock_irqsave(&kbmon.lock, flags);
	start = kbmon.start_jiffies;
	last = kbmon.last_press_jiffies;
	snap->total_presses = kbmon.total_presses;
	snap->repeat_events = kbmon.repeat_events;
	snap->buffer_dropped = kbmon.buffer_dropped;
	snap->buffered_events = kbmon.event_count;
	snap->view = kbmon.view;
	memcpy(event_times, kbmon.event_times, sizeof(event_times));
	event_count = kbmon.event_count;
	spin_unlock_irqrestore(&kbmon.lock, flags);

	snap->active_keyboards = atomic_read(&kbmon.active_keyboards);
	snap->uptime_ms = jiffies64_to_msecs(now - start);
	snap->last_press_ms = last ? jiffies64_to_msecs(last - start) : 0;
	snap->presses_per_minute = snap->uptime_ms ?
		div64_u64(snap->total_presses * 60000ULL, snap->uptime_ms) : 0;
	snap->presses_last_10s = 0;
	snap->peak_presses_per_second = 0;

	for (i = 0; i < event_count; i++) {
		u64 window = 0;

		if (event_times[i] > cutoff_10s)
			snap->presses_last_10s++;

		for (j = 0; j < event_count; j++) {
			if (event_times[j] >= event_times[i] &&
			    event_times[j] < event_times[i] + one_second)
				window++;
		}

		if (window > peak)
			peak = window;
	}

	snap->peak_presses_per_second = peak;
}

static void kbmon_copy_key_counts(u64 *key_counts)
{
	unsigned long flags;

	spin_lock_irqsave(&kbmon.lock, flags);
	memcpy(key_counts, kbmon.key_counts, sizeof(kbmon.key_counts));
	spin_unlock_irqrestore(&kbmon.lock, flags);
}

static void kbmon_copy_events(struct kbmon_events_snapshot *events)
{
	unsigned long flags;
	unsigned int first;
	unsigned int i;

	spin_lock_irqsave(&kbmon.lock, flags);
	events->count = kbmon.event_count;
	events->start_jiffies = kbmon.start_jiffies;
	events->now_jiffies = get_jiffies_64();
	first = (kbmon.event_head + KBMON_EVENT_BUF - kbmon.event_count) %
		KBMON_EVENT_BUF;

	for (i = 0; i < kbmon.event_count; i++) {
		unsigned int index = (first + i) % KBMON_EVENT_BUF;

		events->times[i] = kbmon.event_times[index];
		events->codes[i] = kbmon.event_codes[index];
	}
	spin_unlock_irqrestore(&kbmon.lock, flags);
}

static int kbmon_append(char *out, int size, int len, const char *fmt, ...)
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

static int kbmon_format_summary(char *out, int size,
				const struct kbmon_snapshot *snap)
{
	return scnprintf(out, size,
			 "driver=kbmonitor\n"
			 "view=summary\n"
			 "total_presses=%llu\n"
			 "active_keyboards=%d\n"
			 "uptime_ms=%llu\n"
			 "last_press_ms=%llu\n"
			 "presses_per_minute=%llu\n"
			 "presses_last_10s=%llu\n"
			 "peak_presses_per_second=%llu\n"
			 "repeat_events=%llu\n"
			 "buffered_events=%u\n"
			 "buffer_dropped=%llu\n",
			 (unsigned long long)snap->total_presses,
			 snap->active_keyboards,
			 (unsigned long long)snap->uptime_ms,
			 (unsigned long long)snap->last_press_ms,
			 (unsigned long long)snap->presses_per_minute,
			 (unsigned long long)snap->presses_last_10s,
			 (unsigned long long)snap->peak_presses_per_second,
			 (unsigned long long)snap->repeat_events,
			 snap->buffered_events,
			 (unsigned long long)snap->buffer_dropped);
}

static void kbmon_insert_top_key(unsigned int code, u64 count,
				 unsigned int *top_codes, u64 *top_counts)
{
	int i;
	int j;

	if (!count)
		return;

	for (i = 0; i < KBMON_TOP_KEYS; i++) {
		if (count <= top_counts[i])
			continue;

		for (j = KBMON_TOP_KEYS - 1; j > i; j--) {
			top_counts[j] = top_counts[j - 1];
			top_codes[j] = top_codes[j - 1];
		}

		top_counts[i] = count;
		top_codes[i] = code;
		return;
	}
}

static int kbmon_format_keys(char *out, int size,
			     const struct kbmon_snapshot *snap)
{
	u64 *counts;
	u64 letters = 0;
	u64 digits = 0;
	u64 modifiers = 0;
	u64 navigation = 0;
	u64 function_keys = 0;
	u64 control_keys = 0;
	u64 other_keys = 0;
	u64 top_counts[KBMON_TOP_KEYS] = { 0 };
	unsigned int top_codes[KBMON_TOP_KEYS] = { 0 };
	unsigned int nonzero_keys = 0;
	unsigned int code;
	int len;
	int i;

	counts = kcalloc(KBMON_KEY_COUNT, sizeof(*counts), GFP_KERNEL);
	if (!counts)
		return scnprintf(out, size, "error=ENOMEM\n");

	kbmon_copy_key_counts(counts);

	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		u64 count = counts[code];

		if (!count)
			continue;

		nonzero_keys++;
		kbmon_insert_top_key(code, count, top_codes, top_counts);

		if (kbmon_is_letter_key(code))
			letters += count;
		else if (kbmon_is_digit_key(code))
			digits += count;
		else if (kbmon_is_modifier_key(code))
			modifiers += count;
		else if (kbmon_is_navigation_key(code))
			navigation += count;
		else if (kbmon_is_function_key(code))
			function_keys += count;
		else if (kbmon_is_control_key(code))
			control_keys += count;
		else
			other_keys += count;
	}

	len = scnprintf(out, size,
			"driver=kbmonitor\n"
			"view=keys\n"
			"total_presses=%llu\n"
			"active_keyboards=%d\n"
			"repeat_events=%llu\n"
			"nonzero_keys=%u\n"
			"letters=%llu\n"
			"digits=%llu\n"
			"modifiers=%llu\n"
			"navigation=%llu\n"
			"function_keys=%llu\n"
			"control_keys=%llu\n"
			"other_keys=%llu\n",
			(unsigned long long)snap->total_presses,
			snap->active_keyboards,
			(unsigned long long)snap->repeat_events,
			nonzero_keys,
			(unsigned long long)letters,
			(unsigned long long)digits,
			(unsigned long long)modifiers,
			(unsigned long long)navigation,
			(unsigned long long)function_keys,
			(unsigned long long)control_keys,
			(unsigned long long)other_keys);

	for (i = 0; i < KBMON_TOP_KEYS; i++) {
		len = kbmon_append(out, size, len,
				   "top_%d_key=%s\n"
				   "top_%d_code=%u\n"
				   "top_%d_count=%llu\n",
				   i + 1, kbmon_key_label(top_codes[i]),
				   i + 1, top_codes[i],
				   i + 1,
				   (unsigned long long)top_counts[i]);
	}

	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		if (counts[code]) {
			len = kbmon_append(out, size, len, "key_%u=%llu\n",
					   code,
					   (unsigned long long)counts[code]);
			len = kbmon_append(out, size, len,
					   "key_%u_label=%s\n",
					   code, kbmon_key_label(code));
		}
	}

	kfree(counts);
	return len;
}

static const char *kbmon_view_name(enum kbmon_view view)
{
	switch (view) {
	case KBMON_VIEW_SUMMARY:
		return "summary";
	case KBMON_VIEW_KEYS:
		return "keys";
	case KBMON_VIEW_EVENTS:
		return "events";
	case KBMON_VIEW_STATUS:
		return "status";
	case KBMON_VIEW_HELP:
		return "help";
	case KBMON_VIEW_TEXT:
		return "text";
	default:
		return "unknown";
	}
}

static int kbmon_format_help(char *out, int size)
{
	return scnprintf(out, size,
			 "driver=kbmonitor\n"
			 "view=help\n"
			 "description=USB keyboard activity monitor LKM\n"
			 "\n"
			 "User-space helper commands:\n"
			 "  ./user/kbmon summary        quick activity/count check\n"
			 "  ./user/kbmon keys           key categories, top keys, per-key counts\n"
			 "  ./user/kbmon heatmap        keyboard layout with per-key counts\n"
			 "  ./user/kbmon events         recent keypress history with timing\n"
			 "  ./user/kbmon log            recent Linux key-name log entries\n"
			 "  ./user/kbmon status         driver/device status\n"
			 "  ./user/kbmon export         JSON report evidence\n"
			 "  ./user/kbmon reset          clear counters and log buffer\n"
			 "  ./user/kbmon watch S N      repeat summary every S seconds, N times\n"
			 "  ./user/kbmon help           show this feature list\n"
			 "\n"
			 "Direct /dev/kbmonitor commands:\n"
			 "  help or view help           show this help view\n"
			 "  view summary                select summary output\n"
			 "  view keys                   select key analytics output\n"
			 "  view events                 select recent events output\n"
			 "  view status                 select driver status output\n"
			 "  mode count                  summary mode, disables text mode if built\n"
			 "  mode analytics              key analytics mode\n"
			 "  reset                       clear counters and key-name log\n"
			 "\n"
			 "Optional TEXT_MODE=1 commands:\n"
			 "  mode text                   enable local text demo mode\n"
			 "  view text                   read local text demo buffer\n"
			 "  clear_text                  clear local text demo buffer\n"
			 "\n"
			 "Key-name log device:\n"
			 "  cat /dev/kbmonitor_log      read bounded past key-name log\n"
			 "\n"
			 "TLS helper:\n"
			 "  ./user/kbmon_tls HOST PORT --insecure\n"
			 "  ./user/kbmon_tls HOST PORT --ca-file FILE --server-name NAME\n"
			 "\n"
			 "Privacy:\n"
			 "  Level 1 and Level 2 do not reconstruct typed text.\n"
			 "  TLS export sends key names from /dev/kbmonitor_log, not text mode.\n");
}

static int kbmon_format_status(char *out, int size,
			       const struct kbmon_snapshot *snap)
{
#ifdef ENABLE_TEXT_MODE
	unsigned long flags;
	bool text_mode_enabled = false;

	spin_lock_irqsave(&kbmon.lock, flags);
	text_mode_enabled = kbmon.text_mode_enabled;
	spin_unlock_irqrestore(&kbmon.lock, flags);
#endif

	return scnprintf(out, size,
			 "driver=kbmonitor\n"
			 "view=status\n"
			 "device=/dev/%s\n"
			 "log_device=/dev/kbmonitor_log\n"
			 "module_loaded=yes\n"
			 "active_keyboards=%d\n"
			 "current_view=%s\n"
			 "allow_non_usb=%u\n"
			 "text_mode_build=%s\n"
#ifdef ENABLE_TEXT_MODE
			 "text_mode_enabled=%u\n"
#endif
			 "event_buffer_capacity=%u\n"
			 "buffered_events=%u\n"
			 "buffer_dropped=%llu\n",
			 KBMON_DEVICE_NAME,
			 snap->active_keyboards,
			 kbmon_view_name(snap->view),
			 allow_non_usb ? 1 : 0,
#ifdef ENABLE_TEXT_MODE
			 "yes",
			 text_mode_enabled ? 1 : 0,
#else
			 "no",
#endif
			 KBMON_EVENT_BUF,
			 snap->buffered_events,
			 (unsigned long long)snap->buffer_dropped);
}

static int kbmon_format_events(char *out, int size,
			       const struct kbmon_snapshot *snap)
{
	struct kbmon_events_snapshot events;
	unsigned int i;
	int len;

	kbmon_copy_events(&events);

	len = scnprintf(out, size,
			"driver=kbmonitor\n"
			"view=events\n"
			"event_count=%u\n"
			"event_capacity=%u\n"
			"buffer_dropped=%llu\n",
			events.count,
			KBMON_EVENT_BUF,
			(unsigned long long)snap->buffer_dropped);

	for (i = 0; i < events.count; i++) {
		u64 since_start_ms =
			jiffies64_to_msecs(events.times[i] -
					   events.start_jiffies);
		u64 age_ms =
			jiffies64_to_msecs(events.now_jiffies -
					   events.times[i]);

		len = kbmon_append(out, size, len,
				   "event_%u_ms=%llu\n"
				   "event_%u_age_ms=%llu\n"
				   "event_%u_key=%s\n"
				   "event_%u_code=%u\n",
				   i + 1,
				   (unsigned long long)since_start_ms,
				   i + 1,
				   (unsigned long long)age_ms,
				   i + 1,
				   kbmon_key_label(events.codes[i]),
				   i + 1,
				   events.codes[i]);
	}

	return len;
}

static ssize_t kbmon_read(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	struct kbmon_snapshot snap;
	char *out;
	int len;
	ssize_t ret;

	out = kzalloc(KBMON_OUT_SIZE, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	kbmon_snapshot_stats(&snap);

	if (snap.view == KBMON_VIEW_KEYS)
		len = kbmon_format_keys(out, KBMON_OUT_SIZE, &snap);
	else if (snap.view == KBMON_VIEW_EVENTS)
		len = kbmon_format_events(out, KBMON_OUT_SIZE, &snap);
	else if (snap.view == KBMON_VIEW_STATUS)
		len = kbmon_format_status(out, KBMON_OUT_SIZE, &snap);
	else if (snap.view == KBMON_VIEW_HELP)
		len = kbmon_format_help(out, KBMON_OUT_SIZE);
#ifdef ENABLE_TEXT_MODE
	else if (snap.view == KBMON_VIEW_TEXT)
		len = kbmon_format_text(out, KBMON_OUT_SIZE);
#endif
	else
		len = kbmon_format_summary(out, KBMON_OUT_SIZE, &snap);

	ret = simple_read_from_buffer(user_buf, count, ppos, out, len);
	kfree(out);
	return ret;
}

static ssize_t kbmon_write(struct file *file, const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	char cmd_buf[KBMON_MAX_CMD];
	char *cmd;
	unsigned long flags;

	if (count == 0)
		return 0;

	if (count >= sizeof(cmd_buf)) {
		pr_warn("kbmonitor: rejected overlong command (%zu bytes)\n",
			count);
		return -EINVAL;
	}

	if (copy_from_user(cmd_buf, user_buf, count))
		return -EFAULT;

	cmd_buf[count] = '\0';
	cmd = strim(cmd_buf);

	if (!strcmp(cmd, "reset")) {
		u64 now = get_jiffies_64();

		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon_reset_locked(now);
		kbmon_log_reset();
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: counters reset from user space\n");
		return count;
	}

	if (!strcmp(cmd, "help") || !strcmp(cmd, "view help")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.view = KBMON_VIEW_HELP;
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: help view selected\n");
		return count;
	}

	if (!strcmp(cmd, "view summary")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.view = KBMON_VIEW_SUMMARY;
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: summary view selected\n");
		return count;
	}

	if (!strcmp(cmd, "mode count")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.view = KBMON_VIEW_SUMMARY;
#ifdef ENABLE_TEXT_MODE
		kbmon.text_mode_enabled = false;
#endif
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: count mode selected\n");
		return count;
	}

	if (!strcmp(cmd, "view keys")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.view = KBMON_VIEW_KEYS;
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: key analytics view selected\n");
		return count;
	}

	if (!strcmp(cmd, "view events")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.view = KBMON_VIEW_EVENTS;
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: recent event view selected\n");
		return count;
	}

	if (!strcmp(cmd, "view status")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.view = KBMON_VIEW_STATUS;
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: status view selected\n");
		return count;
	}

	if (!strcmp(cmd, "mode analytics")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.view = KBMON_VIEW_KEYS;
#ifdef ENABLE_TEXT_MODE
		kbmon.text_mode_enabled = false;
#endif
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: analytics mode selected\n");
		return count;
	}

#ifdef ENABLE_TEXT_MODE
	if (!strcmp(cmd, "mode text")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.text_mode_enabled = true;
		kbmon.view = KBMON_VIEW_TEXT;
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_warn("kbmonitor: local text demo mode enabled; demo use only; data is not exported by TLS\n");
		return count;
	}

	if (!strcmp(cmd, "view text")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		if (!kbmon.text_mode_enabled) {
			spin_unlock_irqrestore(&kbmon.lock, flags);
			pr_warn("kbmonitor: rejected text view because text mode is disabled\n");
			return -EPERM;
		}
		kbmon.view = KBMON_VIEW_TEXT;
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		return count;
	}

	if (!strcmp(cmd, "clear_text")) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon_text_clear_locked();
		spin_unlock_irqrestore(&kbmon.lock, flags);
		*ppos = 0;
		pr_info("kbmonitor: local text demo buffer cleared\n");
		return count;
	}
#else
	if (!strcmp(cmd, "view text") || !strcmp(cmd, "mode text") ||
	    !strcmp(cmd, "clear_text")) {
		pr_info("kbmonitor: command '%s' requires rebuild with TEXT_MODE=1\n",
			cmd);
		return -EOPNOTSUPP;
	}
#endif

	pr_warn("kbmonitor: unknown command '%s'\n", cmd);
	return -EINVAL;
}

static int kbmon_open(struct inode *inode, struct file *file)
{
	pr_info("kbmonitor: /dev/%s opened\n", KBMON_DEVICE_NAME);
	return 0;
}

static int kbmon_release(struct inode *inode, struct file *file)
{
	pr_info("kbmonitor: /dev/%s closed\n", KBMON_DEVICE_NAME);
	return 0;
}

static const struct file_operations kbmon_fops = {
	.owner = THIS_MODULE,
	.open = kbmon_open,
	.read = kbmon_read,
	.write = kbmon_write,
	.release = kbmon_release,
	.llseek = noop_llseek,
};

static bool kbmon_is_keyboard(struct input_dev *dev)
{
	if (!allow_non_usb && dev->id.bustype != BUS_USB)
		return false;

	if (!test_bit(EV_KEY, dev->evbit))
		return false;

	/*
	 * Mice and other devices also expose EV_KEY for buttons. Require common
	 * keyboard keys so the monitor only attaches to keyboard-like devices.
	 */
	if (!test_bit(KEY_A, dev->keybit) || !test_bit(KEY_ENTER, dev->keybit))
		return false;

	return true;
}

static void kbmon_input_event(struct input_handle *handle, unsigned int type,
			      unsigned int code, int value)
{
	unsigned long flags;
	u64 total = 0;

	if (type != EV_KEY)
		return;

#ifdef ENABLE_TEXT_MODE
	spin_lock_irqsave(&kbmon.lock, flags);
	kbmon_text_record_locked(code, value);
	spin_unlock_irqrestore(&kbmon.lock, flags);
#endif

	if (value == 1) {
		u64 now = get_jiffies_64();

		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon_record_press_locked(now, code);
		kbmon_log_record(kbmon.start_jiffies, now, code);
		total = kbmon.total_presses;
		spin_unlock_irqrestore(&kbmon.lock, flags);

		if (total == 1 || total % 25 == 0)
			pr_info("kbmonitor: keyboard activity count=%llu\n",
				(unsigned long long)total);
	} else if (value == 2) {
		spin_lock_irqsave(&kbmon.lock, flags);
		kbmon.repeat_events++;
		spin_unlock_irqrestore(&kbmon.lock, flags);
	}
}

static int kbmon_input_connect(struct input_handler *handler,
			       struct input_dev *dev,
			       const struct input_device_id *id)
{
	struct kbmon_input_handle *kh;
	int err;

	if (!kbmon_is_keyboard(dev))
		return -ENODEV;

	kh = kzalloc(sizeof(*kh), GFP_KERNEL);
	if (!kh)
		return -ENOMEM;

	kh->handle.dev = dev;
	kh->handle.handler = handler;
	kh->handle.name = KBMON_DEVICE_NAME;

	err = input_register_handle(&kh->handle);
	if (err)
		goto err_free;

	err = input_open_device(&kh->handle);
	if (err)
		goto err_unregister;

	atomic_inc(&kbmon.active_keyboards);
	pr_info("kbmonitor: attached keyboard '%s' phys='%s' bustype=0x%04x\n",
		dev->name ?: "unknown", dev->phys ?: "unknown",
		dev->id.bustype);
	return 0;

err_unregister:
	input_unregister_handle(&kh->handle);
err_free:
	kfree(kh);
	return err;
}

static void kbmon_input_disconnect(struct input_handle *handle)
{
	struct kbmon_input_handle *kh =
		container_of(handle, struct kbmon_input_handle, handle);

	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(kh);

	atomic_dec(&kbmon.active_keyboards);
	pr_info("kbmonitor: keyboard disconnected, active_keyboards=%d\n",
		atomic_read(&kbmon.active_keyboards));
}

static const struct input_device_id kbmon_input_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};
MODULE_DEVICE_TABLE(input, kbmon_input_ids);

static struct input_handler kbmon_input_handler = {
	.event = kbmon_input_event,
	.connect = kbmon_input_connect,
	.disconnect = kbmon_input_disconnect,
	.name = KBMON_DEVICE_NAME,
	.id_table = kbmon_input_ids,
};

static int kbmon_chrdev_init(void)
{
	int err;

	err = alloc_chrdev_region(&kbmon_devno, 0, 1, KBMON_DEVICE_NAME);
	if (err)
		return err;

	cdev_init(&kbmon_cdev, &kbmon_fops);
	kbmon_cdev.owner = THIS_MODULE;

	err = cdev_add(&kbmon_cdev, kbmon_devno, 1);
	if (err)
		goto err_unregister_region;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	kbmon_class = class_create(KBMON_CLASS_NAME);
#else
	kbmon_class = class_create(THIS_MODULE, KBMON_CLASS_NAME);
#endif
	if (IS_ERR(kbmon_class)) {
		err = PTR_ERR(kbmon_class);
		goto err_cdev_del;
	}

	kbmon_device = device_create(kbmon_class, NULL, kbmon_devno, NULL,
				     KBMON_DEVICE_NAME);
	if (IS_ERR(kbmon_device)) {
		err = PTR_ERR(kbmon_device);
		goto err_class_destroy;
	}

	pr_info("kbmonitor: created /dev/%s major=%d minor=%d\n",
		KBMON_DEVICE_NAME, MAJOR(kbmon_devno), MINOR(kbmon_devno));
	return 0;

err_class_destroy:
	class_destroy(kbmon_class);
err_cdev_del:
	cdev_del(&kbmon_cdev);
err_unregister_region:
	unregister_chrdev_region(kbmon_devno, 1);
	return err;
}

static void kbmon_chrdev_exit(void)
{
	device_destroy(kbmon_class, kbmon_devno);
	class_destroy(kbmon_class);
	cdev_del(&kbmon_cdev);
	unregister_chrdev_region(kbmon_devno, 1);
	pr_info("kbmonitor: removed /dev/%s\n", KBMON_DEVICE_NAME);
}

static int __init kbmon_init(void)
{
	int err;

	spin_lock_init(&kbmon.lock);
	atomic_set(&kbmon.active_keyboards, 0);
	kbmon_reset_locked(get_jiffies_64());

	err = kbmon_chrdev_init();
	if (err) {
		pr_err("kbmonitor: failed to create character device: %d\n",
		       err);
		return err;
	}

	err = kbmon_log_chrdev_init(kbmon_class);
	if (err) {
		pr_err("kbmonitor: failed to create log character device: %d\n",
		       err);
		kbmon_chrdev_exit();
		return err;
	}

	err = input_register_handler(&kbmon_input_handler);
	if (err) {
		pr_err("kbmonitor: failed to register input handler: %d\n",
		       err);
		kbmon_log_chrdev_exit(kbmon_class);
		kbmon_chrdev_exit();
		return err;
	}

	pr_info("kbmonitor: module loaded; monitoring %s keyboard devices\n",
		allow_non_usb ? "USB and non-USB" : "USB");
	return 0;
}

static void __exit kbmon_exit(void)
{
	input_unregister_handler(&kbmon_input_handler);
	kbmon_log_chrdev_exit(kbmon_class);
	kbmon_chrdev_exit();
	pr_info("kbmonitor: module unloaded\n");
}

module_init(kbmon_init);
module_exit(kbmon_exit);

MODULE_AUTHOR("CSC1107 Project Team");
MODULE_DESCRIPTION("USB keyboard activity logger core driver");
MODULE_LICENSE("GPL");
