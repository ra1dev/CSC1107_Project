/*
 * kbmon - user-space client for /dev/kbmonitor.
 *
 * The program demonstrates read() and write() communication with the
 * kbmonitor kernel module.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define KBMON_DEVICE "/dev/kbmonitor"
#define KBMON_READ_BUF 16384
#define KBMON_KEY_COUNT 768
#define KBMON_TOP_KEYS 10

struct key_label {
	unsigned int code;
	const char *label;
};

static const struct key_label key_labels[] = {
	{ 1, "ESC" },
	{ 2, "1" },
	{ 3, "2" },
	{ 4, "3" },
	{ 5, "4" },
	{ 6, "5" },
	{ 7, "6" },
	{ 8, "7" },
	{ 9, "8" },
	{ 10, "9" },
	{ 11, "0" },
	{ 12, "-" },
	{ 13, "=" },
	{ 14, "BSP" },
	{ 15, "TAB" },
	{ 16, "Q" },
	{ 17, "W" },
	{ 18, "E" },
	{ 19, "R" },
	{ 20, "T" },
	{ 21, "Y" },
	{ 22, "U" },
	{ 23, "I" },
	{ 24, "O" },
	{ 25, "P" },
	{ 26, "[" },
	{ 27, "]" },
	{ 28, "ENT" },
	{ 29, "LCTRL" },
	{ 30, "A" },
	{ 31, "S" },
	{ 32, "D" },
	{ 33, "F" },
	{ 34, "G" },
	{ 35, "H" },
	{ 36, "J" },
	{ 37, "K" },
	{ 38, "L" },
	{ 39, ";" },
	{ 40, "'" },
	{ 41, "`" },
	{ 42, "LSHFT" },
	{ 43, "\\" },
	{ 44, "Z" },
	{ 45, "X" },
	{ 46, "C" },
	{ 47, "V" },
	{ 48, "B" },
	{ 49, "N" },
	{ 50, "M" },
	{ 51, "," },
	{ 52, "." },
	{ 53, "/" },
	{ 54, "RSHFT" },
	{ 56, "LALT" },
	{ 57, "SPACE" },
	{ 58, "CAPS" },
	{ 59, "F1" },
	{ 60, "F2" },
	{ 61, "F3" },
	{ 62, "F4" },
	{ 63, "F5" },
	{ 64, "F6" },
	{ 65, "F7" },
	{ 66, "F8" },
	{ 67, "F9" },
	{ 68, "F10" },
	{ 87, "F11" },
	{ 88, "F12" },
	{ 97, "RCTRL" },
	{ 100, "RALT" },
	{ 102, "HOME" },
	{ 103, "UP" },
	{ 104, "PGUP" },
	{ 105, "LEFT" },
	{ 106, "RIGHT" },
	{ 107, "END" },
	{ 108, "DOWN" },
	{ 109, "PGDN" },
	{ 110, "INS" },
	{ 111, "DEL" },
	{ 125, "LMETA" },
	{ 126, "RMETA" },
};

struct heat_key {
	const char *label;
	unsigned int code;
};

struct key_stats {
	unsigned long long total_presses;
	unsigned long long active_keyboards;
	unsigned long long uptime_ms;
	unsigned long long last_press_ms;
	unsigned long long presses_per_minute;
	unsigned long long presses_last_10s;
	unsigned long long peak_presses_per_second;
	unsigned long long repeat_events;
	unsigned long long buffered_events;
	unsigned long long buffer_dropped;
	unsigned long long nonzero_keys;
	unsigned long long letters;
	unsigned long long digits;
	unsigned long long modifiers;
	unsigned long long navigation;
	unsigned long long function_keys;
	unsigned long long control_keys;
	unsigned long long other_keys;
	unsigned long long counts[KBMON_KEY_COUNT];
};

struct event_entry {
	unsigned int index;
	unsigned int code;
	unsigned long long ms;
	unsigned long long age_ms;
	char key[32];
};

struct event_stats {
	unsigned int event_count;
	unsigned int event_capacity;
	unsigned long long buffer_dropped;
	struct event_entry events[64];
};

static const struct heat_key row_functions[] = {
	{ "ESC", 1 }, { "F1", 59 }, { "F2", 60 }, { "F3", 61 },
	{ "F4", 62 }, { "F5", 63 }, { "F6", 64 }, { "F7", 65 },
	{ "F8", 66 }, { "F9", 67 }, { "F10", 68 }, { "F11", 87 },
	{ "F12", 88 },
};

static const struct heat_key row_numbers[] = {
	{ "`", 41 }, { "1", 2 }, { "2", 3 }, { "3", 4 },
	{ "4", 5 }, { "5", 6 }, { "6", 7 }, { "7", 8 },
	{ "8", 9 }, { "9", 10 }, { "0", 11 }, { "-", 12 },
	{ "=", 13 }, { "BSP", 14 },
};

static const struct heat_key row_qwerty[] = {
	{ "TAB", 15 }, { "Q", 16 }, { "W", 17 }, { "E", 18 },
	{ "R", 19 }, { "T", 20 }, { "Y", 21 }, { "U", 22 },
	{ "I", 23 }, { "O", 24 }, { "P", 25 }, { "[", 26 },
	{ "]", 27 }, { "\\", 43 },
};

static const struct heat_key row_home[] = {
	{ "CAPS", 58 }, { "A", 30 }, { "S", 31 }, { "D", 32 },
	{ "F", 33 }, { "G", 34 }, { "H", 35 }, { "J", 36 },
	{ "K", 37 }, { "L", 38 }, { ";", 39 }, { "'", 40 },
	{ "ENT", 28 },
};

static const struct heat_key row_bottom[] = {
	{ "LSHFT", 42 }, { "Z", 44 }, { "X", 45 }, { "C", 46 },
	{ "V", 47 }, { "B", 48 }, { "N", 49 }, { "M", 50 },
	{ ",", 51 }, { ".", 52 }, { "/", 53 }, { "RSHFT", 54 },
};

static const struct heat_key row_space[] = {
	{ "LCTRL", 29 }, { "LALT", 56 }, { "SPACE", 57 },
	{ "RALT", 100 }, { "RCTRL", 97 }, { "LEFT", 105 },
	{ "UP", 103 }, { "DOWN", 108 }, { "RIGHT", 106 },
};

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s summary              Read Level 1 keyboard activity stats\n"
		"  %s keys                 Show human-friendly key analytics\n"
		"  %s heatmap              Render keyboard layout with per-key counts\n"
		"  %s counts               Alias for heatmap\n"
		"  %s events               Show recent keypress event history\n"
		"  %s status               Show driver/device status\n"
		"  %s export               Print report-friendly JSON evidence\n"
		"  %s raw-keys             Print raw driver key analytics\n"
		"  %s text                 Enable and view local text demo buffer\n"
		"  %s clear-text           Clear local text demo buffer\n"
		"  %s disable-text         Disable local text demo mode\n"
		"  %s reset                Reset counters, then read stats\n"
		"  %s watch [sec] [count]  Read stats repeatedly\n"
		"\n"
		"Default command: summary\n",
		prog, prog, prog, prog, prog, prog, prog, prog, prog, prog,
		prog, prog, prog);
}

static void explain_open_error(void)
{
	if (errno == ENOENT) {
		fprintf(stderr,
			"%s does not exist. Load the module first, for example:\n"
			"  sudo insmod kernel/kbmonitor.ko\n",
			KBMON_DEVICE);
	} else if (errno == EACCES) {
		fprintf(stderr,
			"Permission denied opening %s. Try sudo, or run:\n"
			"  sudo chmod 666 %s\n",
			KBMON_DEVICE, KBMON_DEVICE);
	} else {
		perror("open");
	}
}

static int open_device(void)
{
	int fd = open(KBMON_DEVICE, O_RDWR);

	if (fd < 0)
		explain_open_error();

	return fd;
}

static int write_command(int fd, const char *cmd)
{
	size_t len = strlen(cmd);
	ssize_t written = write(fd, cmd, len);

	if (written < 0) {
		fprintf(stderr, "write(\"%s\") failed: %s\n",
			cmd, strerror(errno));
		return -1;
	}

	if ((size_t)written != len) {
		fprintf(stderr, "short write for command \"%s\"\n", cmd);
		return -1;
	}

	return 0;
}

static int read_into_buffer(int fd, char *buf, size_t size)
{
	ssize_t n = read(fd, buf, size - 1);

	if (n < 0) {
		perror("read");
		return -1;
	}

	if (n == 0) {
		fprintf(stderr, "No stats returned from %s\n", KBMON_DEVICE);
		return -1;
	}

	buf[n] = '\0';
	return 0;
}

static int read_stats(int fd)
{
	char buf[KBMON_READ_BUF];

	if (read_into_buffer(fd, buf, sizeof(buf)) < 0)
		return -1;

	fputs(buf, stdout);
	return 0;
}

static int show_view(const char *view_command)
{
	int fd;
	int rc = 0;

	fd = open_device();
	if (fd < 0)
		return 1;

	if (write_command(fd, view_command) < 0)
		rc = 1;
	else if (read_stats(fd) < 0)
		rc = 1;

	close(fd);
	return rc;
}

static int read_view_into_buffer(const char *view_command, char *buf,
				 size_t size)
{
	int fd;
	int rc = 0;

	fd = open_device();
	if (fd < 0)
		return 1;

	if (write_command(fd, view_command) < 0)
		rc = 1;
	else if (read_into_buffer(fd, buf, size) < 0)
		rc = 1;

	close(fd);
	return rc;
}

static int show_summary(void)
{
	return show_view("view summary");
}

static int show_raw_keys(void)
{
	return show_view("view keys");
}

static int reset_and_show(void)
{
	int fd;
	int rc = 0;

	fd = open_device();
	if (fd < 0)
		return 1;

	if (write_command(fd, "reset") < 0)
		rc = 1;
	else if (write_command(fd, "view summary") < 0)
		rc = 1;
	else if (read_stats(fd) < 0)
		rc = 1;

	close(fd);
	return rc;
}

static void print_text_view(const char *buf)
{
	const char *begin = strstr(buf, "text_begin\n");
	const char *end;

	puts("Local text demo buffer");
	puts("This is demo-only output from /dev/kbmonitor. It is not sent remotely.");
	putchar('\n');

	if (!begin) {
		fputs(buf, stdout);
		return;
	}

	begin += strlen("text_begin\n");
	end = strstr(begin, "\ntext_end");
	if (!end) {
		fputs(begin, stdout);
		return;
	}

	if (begin == end) {
		puts("(empty)");
		return;
	}

	fwrite(begin, 1, (size_t)(end - begin), stdout);
	putchar('\n');
}

static int show_text(void)
{
	char buf[KBMON_READ_BUF];
	int fd;
	int rc = 0;

	fd = open_device();
	if (fd < 0)
		return 1;

	if (write_command(fd, "mode text") < 0) {
		fprintf(stderr,
			"Text mode is unavailable unless the module is built with TEXT_MODE=1.\n");
		rc = 1;
	} else if (write_command(fd, "view text") < 0) {
		rc = 1;
	} else if (read_into_buffer(fd, buf, sizeof(buf)) < 0) {
		rc = 1;
	} else {
		print_text_view(buf);
	}

	close(fd);
	return rc;
}

static int clear_text(void)
{
	int fd;
	int rc = 0;

	fd = open_device();
	if (fd < 0)
		return 1;

	if (write_command(fd, "clear_text") < 0) {
		fprintf(stderr,
			"Text mode is unavailable unless the module is built with TEXT_MODE=1.\n");
		rc = 1;
	} else {
		puts("Local text demo buffer cleared.");
	}

	close(fd);
	return rc;
}

static int disable_text(void)
{
	int fd;
	int rc = 0;

	fd = open_device();
	if (fd < 0)
		return 1;

	if (write_command(fd, "mode count") < 0)
		rc = 1;
	else
		puts("Local text demo mode disabled.");

	close(fd);
	return rc;
}

static int parse_positive_int(const char *text, int fallback)
{
	char *end = NULL;
	long value;

	if (!text)
		return fallback;

	errno = 0;
	value = strtol(text, &end, 10);
	if (errno || end == text || *end != '\0' || value <= 0 ||
	    value > 86400)
		return fallback;

	return (int)value;
}

static const char *lookup_key_label(unsigned int code)
{
	static char fallback[16];
	size_t i;

	for (i = 0; i < sizeof(key_labels) / sizeof(key_labels[0]); i++) {
		if (key_labels[i].code == code)
			return key_labels[i].label;
	}

	snprintf(fallback, sizeof(fallback), "KEY_%u", code);
	return fallback;
}

static int parse_key_counts(const char *text,
			    unsigned long long counts[KBMON_KEY_COUNT])
{
	const char *line = text;
	unsigned int code;
	unsigned long long value;
	int parsed = 0;

	while (*line) {
		if (sscanf(line, "key_%u=%llu", &code, &value) == 2 &&
		    code < KBMON_KEY_COUNT) {
			counts[code] = value;
			parsed++;
		}

		line = strchr(line, '\n');
		if (!line)
			break;
		line++;
	}

	return parsed;
}

static int parse_key_stats(const char *text, struct key_stats *stats)
{
	const char *line = text;
	char name[64];
	unsigned int code;
	unsigned long long value;
	int parsed_keys = 0;

	while (*line) {
		if (sscanf(line, "%63[^=]=%llu", name, &value) == 2) {
			if (!strcmp(name, "total_presses"))
				stats->total_presses = value;
			else if (!strcmp(name, "active_keyboards"))
				stats->active_keyboards = value;
			else if (!strcmp(name, "uptime_ms"))
				stats->uptime_ms = value;
			else if (!strcmp(name, "last_press_ms"))
				stats->last_press_ms = value;
			else if (!strcmp(name, "presses_per_minute"))
				stats->presses_per_minute = value;
			else if (!strcmp(name, "presses_last_10s"))
				stats->presses_last_10s = value;
			else if (!strcmp(name, "peak_presses_per_second"))
				stats->peak_presses_per_second = value;
			else if (!strcmp(name, "repeat_events"))
				stats->repeat_events = value;
			else if (!strcmp(name, "buffered_events"))
				stats->buffered_events = value;
			else if (!strcmp(name, "buffer_dropped"))
				stats->buffer_dropped = value;
			else if (!strcmp(name, "nonzero_keys"))
				stats->nonzero_keys = value;
			else if (!strcmp(name, "letters"))
				stats->letters = value;
			else if (!strcmp(name, "digits"))
				stats->digits = value;
			else if (!strcmp(name, "modifiers"))
				stats->modifiers = value;
			else if (!strcmp(name, "navigation"))
				stats->navigation = value;
			else if (!strcmp(name, "function_keys"))
				stats->function_keys = value;
			else if (!strcmp(name, "control_keys"))
				stats->control_keys = value;
			else if (!strcmp(name, "other_keys"))
				stats->other_keys = value;
			else if (sscanf(name, "key_%u", &code) == 1 &&
				 code < KBMON_KEY_COUNT) {
				stats->counts[code] = value;
				parsed_keys++;
			}
		}

		line = strchr(line, '\n');
		if (!line)
			break;
		line++;
	}

	return parsed_keys;
}

static void parse_event_line(struct event_stats *events, unsigned int index,
			     const char *field, const char *value_text)
{
	struct event_entry *event;

	if (index == 0 || index > sizeof(events->events) / sizeof(events->events[0]))
		return;

	event = &events->events[index - 1];
	event->index = index;

	if (!strcmp(field, "ms"))
		event->ms = strtoull(value_text, NULL, 10);
	else if (!strcmp(field, "age_ms"))
		event->age_ms = strtoull(value_text, NULL, 10);
	else if (!strcmp(field, "code"))
		event->code = (unsigned int)strtoul(value_text, NULL, 10);
	else if (!strcmp(field, "key")) {
		snprintf(event->key, sizeof(event->key), "%s", value_text);
	}
}

static int parse_event_stats(const char *text, struct event_stats *events)
{
	const char *line = text;
	char name[64];
	char value_text[64];
	unsigned long long value;
	unsigned int index;
	char field[32];

	memset(events, 0, sizeof(*events));

	while (*line) {
		if (sscanf(line, "%63[^=]=%63s", name, value_text) == 2) {
			value = strtoull(value_text, NULL, 10);

			if (!strcmp(name, "event_count"))
				events->event_count = (unsigned int)value;
			else if (!strcmp(name, "event_capacity"))
				events->event_capacity = (unsigned int)value;
			else if (!strcmp(name, "buffer_dropped"))
				events->buffer_dropped = value;
			else if (sscanf(name, "event_%u_%31s", &index, field) == 2)
				parse_event_line(events, index, field, value_text);
		}

		line = strchr(line, '\n');
		if (!line)
			break;
		line++;
	}

	return (int)events->event_count;
}

static void print_count_row(const struct heat_key *row, size_t count,
			    const unsigned long long counts[KBMON_KEY_COUNT])
{
	size_t i;

	for (i = 0; i < count; i++) {
		unsigned int code = row[i].code;
		unsigned long long value =
			code < KBMON_KEY_COUNT ? counts[code] : 0;

		printf("[%5s:%-3llu]", row[i].label, value);
	}
	putchar('\n');
}

static void print_top_keys(const unsigned long long counts[KBMON_KEY_COUNT])
{
	unsigned int top_codes[KBMON_TOP_KEYS] = { 0 };
	unsigned long long top_counts[KBMON_TOP_KEYS] = { 0 };
	unsigned int code;
	int i;
	int j;

	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		if (counts[code] == 0)
			continue;

		for (i = 0; i < KBMON_TOP_KEYS; i++) {
			if (counts[code] <= top_counts[i])
				continue;

			for (j = KBMON_TOP_KEYS - 1; j > i; j--) {
				top_counts[j] = top_counts[j - 1];
				top_codes[j] = top_codes[j - 1];
			}

			top_counts[i] = counts[code];
			top_codes[i] = code;
			break;
		}
	}

	puts("\nTop keys:");
	for (i = 0; i < KBMON_TOP_KEYS && top_counts[i] > 0; i++) {
		printf("%2d. %-10s code=%-3u count=%llu\n", i + 1,
		       lookup_key_label(top_codes[i]), top_codes[i],
		       top_counts[i]);
	}
}

static int collect_key_stats(struct key_stats *stats)
{
	char summary_buf[KBMON_READ_BUF];
	char key_buf[KBMON_READ_BUF];

	memset(stats, 0, sizeof(*stats));

	if (read_view_into_buffer("view summary", summary_buf,
				  sizeof(summary_buf)) != 0)
		return 1;

	if (read_view_into_buffer("view keys", key_buf, sizeof(key_buf)) != 0)
		return 1;

	parse_key_stats(summary_buf, stats);
	parse_key_stats(key_buf, stats);
	return 0;
}

static int show_keys(void)
{
	struct key_stats stats;
	unsigned int code;

	if (collect_key_stats(&stats) != 0)
		return 1;

	puts("Keyboard analytics");
	printf("Total key presses: %llu\n", stats.total_presses);
	printf("Active keyboard devices: %llu\n", stats.active_keyboards);
	printf("Repeat events: %llu\n", stats.repeat_events);
	printf("Unique keys pressed: %llu\n", stats.nonzero_keys);
	printf("Uptime: %llums\n", stats.uptime_ms);
	printf("Last press: %llums after module reset/load\n",
	       stats.last_press_ms);
	printf("Average rate: %llu presses/min\n", stats.presses_per_minute);
	printf("Recent rate: %llu presses in the last 10 seconds\n",
	       stats.presses_last_10s);
	printf("Peak observed rate: %llu presses/sec\n",
	       stats.peak_presses_per_second);

	puts("\nCategories:");
	printf("  Letters:       %llu\n", stats.letters);
	printf("  Digits:        %llu\n", stats.digits);
	printf("  Modifiers:     %llu\n", stats.modifiers);
	printf("  Navigation:    %llu\n", stats.navigation);
	printf("  Function keys: %llu\n", stats.function_keys);
	printf("  Control keys:  %llu\n", stats.control_keys);
	printf("  Other keys:    %llu\n", stats.other_keys);

	print_top_keys(stats.counts);

	puts("\nPressed keys:");
	printf("  %-10s %-6s %s\n", "KEY", "CODE", "COUNT");
	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		if (stats.counts[code])
			printf("  %-10s %-6u %llu\n", lookup_key_label(code),
			       code, stats.counts[code]);
	}

	return 0;
}

static int show_heatmap(void)
{
	unsigned long long counts[KBMON_KEY_COUNT] = { 0 };
	char buf[KBMON_READ_BUF];
	unsigned int code;
	unsigned long long total = 0;
	int parsed;
	int fd;
	int rc = 0;

	fd = open_device();
	if (fd < 0)
		return 1;

	if (write_command(fd, "view keys") < 0) {
		close(fd);
		return 1;
	}

	if (read_into_buffer(fd, buf, sizeof(buf)) < 0) {
		close(fd);
		return 1;
	}

	close(fd);

	parsed = parse_key_counts(buf, counts);
	if (parsed == 0) {
		puts("No per-key data yet. Press some keys and run heatmap again.");
		return 0;
	}

	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		total += counts[code];
	}

	printf("Keyboard heatmap: per-key counts (total=%llu)\n", total);
	puts("Format: [KEY:count]");
	putchar('\n');

	print_count_row(row_functions,
			sizeof(row_functions) / sizeof(row_functions[0]),
			counts);
	print_count_row(row_numbers,
			sizeof(row_numbers) / sizeof(row_numbers[0]),
			counts);
	print_count_row(row_qwerty,
			sizeof(row_qwerty) / sizeof(row_qwerty[0]),
			counts);
	print_count_row(row_home, sizeof(row_home) / sizeof(row_home[0]),
			counts);
	print_count_row(row_bottom,
			sizeof(row_bottom) / sizeof(row_bottom[0]),
			counts);
	print_count_row(row_space, sizeof(row_space) / sizeof(row_space[0]),
			counts);

	print_top_keys(counts);
	return rc;
}

static int show_status(void)
{
	return show_view("view status");
}

static int show_events(void)
{
	char buf[KBMON_READ_BUF];
	struct event_stats events;
	unsigned int i;

	if (read_view_into_buffer("view events", buf, sizeof(buf)) != 0)
		return 1;

	parse_event_stats(buf, &events);

	printf("Recent keypress events (%u/%u buffered, dropped=%llu)\n",
	       events.event_count, events.event_capacity,
	       events.buffer_dropped);

	if (events.event_count == 0) {
		puts("No keypress events buffered yet.");
		return 0;
	}

	printf("%-5s %-10s %-6s %-12s %s\n", "NO", "KEY", "CODE",
	       "SINCE_START", "AGE");
	for (i = 0; i < events.event_count &&
	     i < sizeof(events.events) / sizeof(events.events[0]); i++) {
		const struct event_entry *event = &events.events[i];
		const char *key = event->key[0] ? event->key :
			lookup_key_label(event->code);

		printf("%-5u %-10s %-6u %-12llums %llums ago\n",
		       event->index, key, event->code, event->ms,
		       event->age_ms);
	}

	return 0;
}

static void print_json_escaped(const char *text)
{
	putchar('"');
	while (*text) {
		if (*text == '"' || *text == '\\')
			putchar('\\');
		putchar(*text++);
	}
	putchar('"');
}

static int export_json(void)
{
	struct key_stats stats;
	struct event_stats events;
	char event_buf[KBMON_READ_BUF];
	unsigned int code;
	unsigned int i;
	int first = 1;
	time_t now = time(NULL);

	if (collect_key_stats(&stats) != 0)
		return 1;

	if (read_view_into_buffer("view events", event_buf,
				  sizeof(event_buf)) != 0)
		return 1;

	parse_event_stats(event_buf, &events);

	printf("{\n");
	printf("  \"schema\": \"kbmonitor.report.v1\",\n");
	printf("  \"source\": \"kbmonitor\",\n");
	printf("  \"unix_time\": %lld,\n", (long long)now);
	printf("  \"privacy\": { \"exports_text\": false },\n");
	printf("  \"summary\": {\n");
	printf("    \"total_presses\": %llu,\n", stats.total_presses);
	printf("    \"active_keyboards\": %llu,\n", stats.active_keyboards);
	printf("    \"uptime_ms\": %llu,\n", stats.uptime_ms);
	printf("    \"last_press_ms\": %llu,\n", stats.last_press_ms);
	printf("    \"presses_per_minute\": %llu,\n",
	       stats.presses_per_minute);
	printf("    \"presses_last_10s\": %llu,\n", stats.presses_last_10s);
	printf("    \"peak_presses_per_second\": %llu,\n",
	       stats.peak_presses_per_second);
	printf("    \"repeat_events\": %llu,\n", stats.repeat_events);
	printf("    \"buffered_events\": %llu,\n", stats.buffered_events);
	printf("    \"buffer_dropped\": %llu\n", stats.buffer_dropped);
	printf("  },\n");
	printf("  \"analytics\": {\n");
	printf("    \"unique_keys\": %llu,\n", stats.nonzero_keys);
	printf("    \"categories\": {\n");
	printf("      \"letters\": %llu,\n", stats.letters);
	printf("      \"digits\": %llu,\n", stats.digits);
	printf("      \"modifiers\": %llu,\n", stats.modifiers);
	printf("      \"navigation\": %llu,\n", stats.navigation);
	printf("      \"function_keys\": %llu,\n", stats.function_keys);
	printf("      \"control_keys\": %llu,\n", stats.control_keys);
	printf("      \"other_keys\": %llu\n", stats.other_keys);
	printf("    },\n");
	printf("    \"per_key\": [\n");

	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		if (!stats.counts[code])
			continue;

		printf("%s      { \"key\": ", first ? "" : ",\n");
		print_json_escaped(lookup_key_label(code));
		printf(", \"code\": %u, \"count\": %llu }", code,
		       stats.counts[code]);
		first = 0;
	}

	printf("\n    ]\n");
	printf("  },\n");
	printf("  \"recent_events\": [\n");
	for (i = 0; i < events.event_count &&
	     i < sizeof(events.events) / sizeof(events.events[0]); i++) {
		const struct event_entry *event = &events.events[i];
		const char *key = event->key[0] ? event->key :
			lookup_key_label(event->code);

		printf("%s    { \"index\": %u, \"key\": ",
		       i == 0 ? "" : ",\n", event->index);
		print_json_escaped(key);
		printf(", \"code\": %u, \"ms\": %llu, \"age_ms\": %llu }",
		       event->code, event->ms, event->age_ms);
	}
	printf("\n  ]\n");
	printf("}\n");

	return 0;
}

static int watch_summary(int interval_sec, int count)
{
	int i = 0;

	while (count == 0 || i < count) {
		printf("---- sample %d ----\n", i + 1);
		if (show_summary() != 0)
			return 1;
		fflush(stdout);
		i++;
		if (count == 0 || i < count)
			sleep((unsigned int)interval_sec);
	}

	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd = argc >= 2 ? argv[1] : "summary";

	if (!strcmp(cmd, "summary"))
		return show_summary();

	if (!strcmp(cmd, "keys"))
		return show_keys();

	if (!strcmp(cmd, "heatmap") || !strcmp(cmd, "counts"))
		return show_heatmap();

	if (!strcmp(cmd, "events"))
		return show_events();

	if (!strcmp(cmd, "status"))
		return show_status();

	if (!strcmp(cmd, "export"))
		return export_json();

	if (!strcmp(cmd, "raw-keys"))
		return show_raw_keys();

	if (!strcmp(cmd, "text"))
		return show_text();

	if (!strcmp(cmd, "clear-text"))
		return clear_text();

	if (!strcmp(cmd, "disable-text"))
		return disable_text();

	if (!strcmp(cmd, "reset"))
		return reset_and_show();

	if (!strcmp(cmd, "watch")) {
		int interval = parse_positive_int(argc >= 3 ? argv[2] : NULL, 1);
		int count = parse_positive_int(argc >= 4 ? argv[3] : NULL, 0);

		return watch_summary(interval, count);
	}

	usage(argv[0]);
	return 1;
}
