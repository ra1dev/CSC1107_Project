/*
 * kbmon_tls - TLS exporter for kbmonitor Level 1/2 statistics.
 *
 * This program reads only "view summary" and "view keys" from /dev/kbmonitor,
 * serializes the statistics as JSON, and sends them to a TLS server. It never
 * reads "view text" and never transmits reconstructed text.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define KBMON_DEVICE "/dev/kbmonitor"
#define KBMON_READ_BUF 16384
#define KBMON_KEY_COUNT 768
#define KBMON_TOP_KEYS 10
#define JSON_BUF_SIZE 65536

struct options {
	const char *host;
	const char *port;
	const char *ca_file;
	const char *server_name;
	int insecure;
	int interval_sec;
	int count;
};

struct kb_stats {
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
	unsigned long long key_counts[KBMON_KEY_COUNT];
};

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s HOST PORT [--interval SEC] [--count N] [--ca-file FILE] [--server-name NAME] [--insecure]\n"
		"\n"
		"Examples:\n"
		"  %s 192.168.1.20 8443 --insecure\n"
		"  %s 192.168.1.20 8443 --interval 5 --count 10 --insecure\n",
		prog, prog, prog);
}

static const char *key_label(unsigned int code)
{
	switch (code) {
	case 1:
		return "ESC";
	case 2:
		return "1";
	case 3:
		return "2";
	case 4:
		return "3";
	case 5:
		return "4";
	case 6:
		return "5";
	case 7:
		return "6";
	case 8:
		return "7";
	case 9:
		return "8";
	case 10:
		return "9";
	case 11:
		return "0";
	case 12:
		return "MINUS";
	case 13:
		return "EQUAL";
	case 14:
		return "BACKSPACE";
	case 15:
		return "TAB";
	case 16:
		return "Q";
	case 17:
		return "W";
	case 18:
		return "E";
	case 19:
		return "R";
	case 20:
		return "T";
	case 21:
		return "Y";
	case 22:
		return "U";
	case 23:
		return "I";
	case 24:
		return "O";
	case 25:
		return "P";
	case 26:
		return "LEFTBRACE";
	case 27:
		return "RIGHTBRACE";
	case 28:
		return "ENTER";
	case 29:
		return "LEFTCTRL";
	case 30:
		return "A";
	case 31:
		return "S";
	case 32:
		return "D";
	case 33:
		return "F";
	case 34:
		return "G";
	case 35:
		return "H";
	case 36:
		return "J";
	case 37:
		return "K";
	case 38:
		return "L";
	case 39:
		return "SEMICOLON";
	case 40:
		return "APOSTROPHE";
	case 41:
		return "GRAVE";
	case 42:
		return "LEFTSHIFT";
	case 43:
		return "BACKSLASH";
	case 44:
		return "Z";
	case 45:
		return "X";
	case 46:
		return "C";
	case 47:
		return "V";
	case 48:
		return "B";
	case 49:
		return "N";
	case 50:
		return "M";
	case 51:
		return "COMMA";
	case 52:
		return "DOT";
	case 53:
		return "SLASH";
	case 54:
		return "RIGHTSHIFT";
	case 56:
		return "LEFTALT";
	case 57:
		return "SPACE";
	case 58:
		return "CAPSLOCK";
	case 59:
		return "F1";
	case 60:
		return "F2";
	case 61:
		return "F3";
	case 62:
		return "F4";
	case 63:
		return "F5";
	case 64:
		return "F6";
	case 65:
		return "F7";
	case 66:
		return "F8";
	case 67:
		return "F9";
	case 68:
		return "F10";
	case 87:
		return "F11";
	case 88:
		return "F12";
	case 97:
		return "RIGHTCTRL";
	case 100:
		return "RIGHTALT";
	case 102:
		return "HOME";
	case 103:
		return "UP";
	case 104:
		return "PAGEUP";
	case 105:
		return "LEFT";
	case 106:
		return "RIGHT";
	case 107:
		return "END";
	case 108:
		return "DOWN";
	case 109:
		return "PAGEDOWN";
	case 110:
		return "INSERT";
	case 111:
		return "DELETE";
	default:
		break;
	}

	return NULL;
}

static int appendf(char *buf, size_t cap, size_t *len, const char *fmt, ...)
{
	va_list args;
	int written;

	if (*len >= cap)
		return -1;

	va_start(args, fmt);
	written = vsnprintf(buf + *len, cap - *len, fmt, args);
	va_end(args);

	if (written < 0 || (size_t)written >= cap - *len)
		return -1;

	*len += (size_t)written;
	return 0;
}

static int open_device(void)
{
	int fd = open(KBMON_DEVICE, O_RDWR);

	if (fd < 0)
		perror("open " KBMON_DEVICE);

	return fd;
}

static int write_command(int fd, const char *cmd)
{
	size_t len = strlen(cmd);
	ssize_t written = write(fd, cmd, len);

	if (written < 0) {
		fprintf(stderr, "write(\"%s\") failed: %s\n", cmd,
			strerror(errno));
		return -1;
	}

	return (size_t)written == len ? 0 : -1;
}

static int read_view(const char *view_cmd, char *buf, size_t size)
{
	int fd;
	ssize_t n;

	fd = open_device();
	if (fd < 0)
		return -1;

	if (write_command(fd, view_cmd) < 0) {
		close(fd);
		return -1;
	}

	n = read(fd, buf, size - 1);
	if (n < 0) {
		perror("read " KBMON_DEVICE);
		close(fd);
		return -1;
	}

	buf[n] = '\0';
	close(fd);
	return 0;
}

static void parse_kv_text(const char *text, struct kb_stats *stats)
{
	const char *line = text;
	char name[64];
	unsigned int code;
	unsigned long long value;

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
				 code < KBMON_KEY_COUNT)
				stats->key_counts[code] = value;
		}

		line = strchr(line, '\n');
		if (!line)
			break;
		line++;
	}
}

static int collect_stats(struct kb_stats *stats)
{
	char summary[KBMON_READ_BUF];
	char keys[KBMON_READ_BUF];

	memset(stats, 0, sizeof(*stats));

	if (read_view("view summary", summary, sizeof(summary)) < 0)
		return -1;

	if (read_view("view keys", keys, sizeof(keys)) < 0)
		return -1;

	parse_kv_text(summary, stats);
	parse_kv_text(keys, stats);
	return 0;
}

static void insert_top_key(unsigned int code, unsigned long long count,
			   unsigned int *top_codes,
			   unsigned long long *top_counts)
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

static int build_json(const struct kb_stats *stats, char *json, size_t cap)
{
	unsigned int top_codes[KBMON_TOP_KEYS] = { 0 };
	unsigned long long top_counts[KBMON_TOP_KEYS] = { 0 };
	char hostname[128] = "unknown";
	size_t len = 0;
	int first;
	int i;
	unsigned int code;
	time_t now = time(NULL);

	(void)gethostname(hostname, sizeof(hostname));
	hostname[sizeof(hostname) - 1] = '\0';

	for (code = 0; code < KBMON_KEY_COUNT; code++)
		insert_top_key(code, stats->key_counts[code], top_codes,
			       top_counts);

	if (appendf(json, cap, &len,
		    "{"
		    "\"schema\":\"kbmonitor.stats.v1\","
		    "\"source\":\"kbmonitor\","
		    "\"host\":\"%s\","
		    "\"unix_time\":%lld,"
		    "\"privacy\":{\"exports_text\":false},",
		    hostname, (long long)now) < 0)
		return -1;

	if (appendf(json, cap, &len,
		    "\"summary\":{"
		    "\"total_presses\":%llu,"
		    "\"active_keyboards\":%llu,"
		    "\"uptime_ms\":%llu,"
		    "\"last_press_ms\":%llu,"
		    "\"presses_per_minute\":%llu,"
		    "\"presses_last_10s\":%llu,"
		    "\"peak_presses_per_second\":%llu,"
		    "\"repeat_events\":%llu,"
		    "\"buffered_events\":%llu,"
		    "\"buffer_dropped\":%llu"
		    "},",
		    stats->total_presses, stats->active_keyboards,
		    stats->uptime_ms, stats->last_press_ms,
		    stats->presses_per_minute, stats->presses_last_10s,
		    stats->peak_presses_per_second,
		    stats->repeat_events, stats->buffered_events,
		    stats->buffer_dropped) < 0)
		return -1;

	if (appendf(json, cap, &len,
		    "\"analytics\":{"
		    "\"unique_keys\":%llu,"
		    "\"categories\":{"
		    "\"letters\":%llu,"
		    "\"digits\":%llu,"
		    "\"modifiers\":%llu,"
		    "\"navigation\":%llu,"
		    "\"function_keys\":%llu,"
		    "\"control_keys\":%llu,"
		    "\"other_keys\":%llu"
		    "},",
		    stats->nonzero_keys, stats->letters, stats->digits,
		    stats->modifiers, stats->navigation, stats->function_keys,
		    stats->control_keys, stats->other_keys) < 0)
		return -1;

	if (appendf(json, cap, &len, "\"top_keys\":[") < 0)
		return -1;

	first = 1;
	for (i = 0; i < KBMON_TOP_KEYS && top_counts[i] > 0; i++) {
		const char *label = key_label(top_codes[i]);
		char fallback[24];

		if (!label) {
			snprintf(fallback, sizeof(fallback), "KEY_%u",
				 top_codes[i]);
			label = fallback;
		}

		if (appendf(json, cap, &len,
			    "%s{\"key\":\"%s\",\"code\":%u,\"count\":%llu}",
			    first ? "" : ",", label, top_codes[i],
			    top_counts[i]) < 0)
			return -1;
		first = 0;
	}

	if (appendf(json, cap, &len, "],\"per_key\":{") < 0)
		return -1;

	first = 1;
	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		const char *label;
		char fallback[24];

		if (!stats->key_counts[code])
			continue;

		label = key_label(code);
		if (!label) {
			snprintf(fallback, sizeof(fallback), "KEY_%u", code);
			label = fallback;
		}

		if (appendf(json, cap, &len, "%s\"%s\":%llu",
			    first ? "" : ",", label,
			    stats->key_counts[code]) < 0)
			return -1;
		first = 0;
	}

	if (appendf(json, cap, &len, "}}}\n") < 0)
		return -1;

	return 0;
}

static int tcp_connect(const char *host, const char *port)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *rp;
	int fd = -1;
	int err;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;

	err = getaddrinfo(host, port, &hints, &res);
	if (err) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
		return -1;
	}

	for (rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;

		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(fd);
		fd = -1;
	}

	freeaddrinfo(res);
	return fd;
}

static int ssl_write_all(SSL *ssl, const char *buf, size_t len)
{
	size_t sent = 0;

	while (sent < len) {
		int n = SSL_write(ssl, buf + sent, (int)(len - sent));

		if (n <= 0)
			return -1;

		sent += (size_t)n;
	}

	return 0;
}

static int send_tls_payload(const struct options *opts, const char *payload)
{
	SSL_CTX *ctx;
	SSL *ssl;
	int fd;
	int rc = -1;

	fd = tcp_connect(opts->host, opts->port);
	if (fd < 0) {
		perror("connect");
		return -1;
	}

	ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx) {
		close(fd);
		return -1;
	}

	if (opts->ca_file) {
		if (SSL_CTX_load_verify_locations(ctx, opts->ca_file, NULL) != 1) {
			ERR_print_errors_fp(stderr);
			goto out_ctx;
		}
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
	} else {
		SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
		if (!opts->insecure)
			fprintf(stderr,
				"warning: peer verification disabled; use --ca-file for verification\n");
	}

	ssl = SSL_new(ctx);
	if (!ssl)
		goto out_ctx;

	SSL_set_fd(ssl, fd);
	SSL_set_tlsext_host_name(ssl, opts->server_name);
	if (opts->ca_file)
		SSL_set1_host(ssl, opts->server_name);

	if (SSL_connect(ssl) != 1) {
		ERR_print_errors_fp(stderr);
		goto out_ssl;
	}

	if (ssl_write_all(ssl, payload, strlen(payload)) == 0)
		rc = 0;
	else
		ERR_print_errors_fp(stderr);

	SSL_shutdown(ssl);

out_ssl:
	SSL_free(ssl);
out_ctx:
	SSL_CTX_free(ctx);
	close(fd);
	return rc;
}

static int parse_int_arg(const char *value, int fallback)
{
	char *end = NULL;
	long parsed;

	errno = 0;
	parsed = strtol(value, &end, 10);
	if (errno || end == value || *end != '\0' || parsed < 0 ||
	    parsed > 86400)
		return fallback;

	return (int)parsed;
}

static int parse_args(int argc, char **argv, struct options *opts)
{
	int i;

	memset(opts, 0, sizeof(*opts));
	opts->interval_sec = 5;
	opts->count = 1;

	if (argc < 3)
		return -1;

	opts->host = argv[1];
	opts->port = argv[2];
	opts->server_name = opts->host;

	for (i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--interval") && i + 1 < argc)
			opts->interval_sec = parse_int_arg(argv[++i], 5);
		else if (!strcmp(argv[i], "--count") && i + 1 < argc)
			opts->count = parse_int_arg(argv[++i], 1);
		else if (!strcmp(argv[i], "--ca-file") && i + 1 < argc)
			opts->ca_file = argv[++i];
		else if (!strcmp(argv[i], "--server-name") && i + 1 < argc)
			opts->server_name = argv[++i];
		else if (!strcmp(argv[i], "--insecure"))
			opts->insecure = 1;
		else
			return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct options opts;
	int sent = 0;

	if (parse_args(argc, argv, &opts) < 0) {
		usage(argv[0]);
		return 1;
	}

	SSL_library_init();
	SSL_load_error_strings();

	while (opts.count == 0 || sent < opts.count) {
		struct kb_stats stats;
		char json[JSON_BUF_SIZE];

		if (collect_stats(&stats) < 0)
			return 1;

		if (build_json(&stats, json, sizeof(json)) < 0) {
			fprintf(stderr, "failed to build JSON payload\n");
			return 1;
		}

		if (send_tls_payload(&opts, json) < 0)
			return 1;

		printf("sent TLS stats sample %d to %s:%s\n", sent + 1,
		       opts.host, opts.port);
		sent++;

		if (opts.count == 0 || sent < opts.count)
			sleep((unsigned int)opts.interval_sec);
	}

	return 0;
}
