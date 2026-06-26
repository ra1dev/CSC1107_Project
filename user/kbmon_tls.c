/*
 * kbmon_tls - TLS statistics logger for kbmonitor.
 *
 * Reads aggregate keyboard statistics from /dev/kbmonitor and sends them
 * encrypted to a remote TLS server on a configurable interval. Individual
 * keystrokes are never transmitted; only aggregate counts are exported.
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

#define KBMON_DEVICE    "/dev/kbmonitor"
#define KBMON_READ_BUF  16384
#define KBMON_KEY_COUNT 768
#define KBMON_TOP_KEYS  10
#define KEY_LABEL_MAX   32
#define JSON_BUF_SIZE       65536
#define DEFAULT_CA_FILE     "server/server.crt"
#define DEFAULT_CLIENT_CERT "server/client.crt"
#define DEFAULT_CLIENT_KEY  "server/client.key"

struct options {
	const char *host;
	const char *port;
	const char *ca_file;
	const char *server_name;
	const char *client_cert;
	const char *client_key;
	int insecure;
	int interval_sec;
	int count;
};

struct tls_client {
	SSL_CTX *ctx;
	SSL *ssl;
	int fd;
};

struct kb_stats {
	unsigned long long total_presses;
	unsigned long long uptime_ms;
	unsigned long long presses_per_minute;
	unsigned long long presses_last_10s;
	unsigned long long peak_presses_per_second;
	unsigned long long repeat_events;
	unsigned long long buffer_dropped;
	int active_keyboards;
	unsigned long long letters;
	unsigned long long digits;
	unsigned long long modifiers;
	unsigned long long navigation;
	unsigned long long function_keys;
	unsigned long long control_keys;
	unsigned long long other_keys;
	unsigned long long counts[KBMON_KEY_COUNT];
	char labels[KBMON_KEY_COUNT][KEY_LABEL_MAX];
};

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s HOST PORT [--interval SEC] [--count N]\n"
		"              [--ca-file FILE] [--client-cert FILE] [--client-key FILE]\n"
		"              [--server-name NAME] [--insecure]\n"
		"\n"
		"Sends encrypted keyboard statistics snapshots using mutual TLS.\n"
		"Only aggregate counts are transmitted; no individual keystrokes are sent.\n"
		"\n"
		"Options:\n"
		"  --interval SEC      Send a snapshot every SEC seconds (default: 5)\n"
		"  --count N           Stop after N snapshots (default: run until interrupted)\n"
		"  --ca-file FILE      CA cert to verify the server (default: server/server.crt)\n"
		"  --client-cert FILE  Client cert to send to the server (default: server/client.crt)\n"
		"  --client-key FILE   Client private key (default: server/client.key)\n"
		"  --server-name NAME  TLS SNI hostname (default: HOST)\n"
		"  --insecure          Disable server certificate verification\n"
		"\n"
		"Run scripts/generate_tls_cert.sh <SERVER_IP> to generate all certificates.\n"
		"\n"
		"Examples:\n"
		"  %s 192.168.1.20 8443\n"
		"  %s 192.168.1.20 8443 --interval 10 --count 5\n",
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

static int kbmon_read_view(const char *cmd, char *buf, size_t size)
{
	int fd;
	ssize_t n;

	fd = open(KBMON_DEVICE, O_RDWR);
	if (fd < 0) {
		if (errno == ENOENT)
			fprintf(stderr,
				"%s not found; load the kbmonitor module first\n",
				KBMON_DEVICE);
		else
			perror("open " KBMON_DEVICE);
		return -1;
	}

	if (write(fd, cmd, strlen(cmd)) < 0) {
		perror("write");
		close(fd);
		return -1;
	}

	n = read(fd, buf, size - 1);
	if (n < 0) {
		perror("read");
		close(fd);
		return -1;
	}

	buf[n] = '\0';
	close(fd);
	return 0;
}

static void parse_named_stat(struct kb_stats *stats, const char *name,
			     unsigned long long value)
{
	if (!strcmp(name, "total_presses"))
		stats->total_presses = value;
	else if (!strcmp(name, "uptime_ms"))
		stats->uptime_ms = value;
	else if (!strcmp(name, "presses_per_minute"))
		stats->presses_per_minute = value;
	else if (!strcmp(name, "presses_last_10s"))
		stats->presses_last_10s = value;
	else if (!strcmp(name, "peak_presses_per_second"))
		stats->peak_presses_per_second = value;
	else if (!strcmp(name, "repeat_events"))
		stats->repeat_events = value;
	else if (!strcmp(name, "buffer_dropped"))
		stats->buffer_dropped = value;
	else if (!strcmp(name, "active_keyboards"))
		stats->active_keyboards = (int)value;
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
}

static void parse_stats_text(const char *text, struct kb_stats *stats)
{
	const char *line = text;
	char name[64];
	char label[KEY_LABEL_MAX];
	unsigned long long value;
	unsigned int code;

	while (*line) {
		/* key_N_label=LABEL — must check before key_N= to avoid
		 * partial match on the code number */
		if (sscanf(line, "key_%u_label=%31s", &code, label) == 2 &&
		    code < KBMON_KEY_COUNT) {
			snprintf(stats->labels[code], KEY_LABEL_MAX, "%s",
				 label);
		} else if (sscanf(line, "key_%u=%llu", &code, &value) == 2 &&
			   code < KBMON_KEY_COUNT) {
			stats->counts[code] = value;
		} else if (sscanf(line, "%63[^=]=%llu", name, &value) == 2) {
			parse_named_stat(stats, name, value);
		}

		line = strchr(line, '\n');
		if (!line)
			break;
		line++;
	}
}

static int collect_stats(struct kb_stats *stats)
{
	char buf[KBMON_READ_BUF];

	memset(stats, 0, sizeof(*stats));

	if (kbmon_read_view("view summary", buf, sizeof(buf)) < 0)
		return -1;
	parse_stats_text(buf, stats);

	if (kbmon_read_view("view keys", buf, sizeof(buf)) < 0)
		return -1;
	parse_stats_text(buf, stats);

	return 0;
}

static void compute_top_keys(const struct kb_stats *stats,
			     unsigned int top_codes[KBMON_TOP_KEYS],
			     int *top_count_out)
{
	unsigned long long top_counts[KBMON_TOP_KEYS] = { 0 };
	unsigned int code;
	int n = 0;
	int i;
	int j;

	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		unsigned long long cnt = stats->counts[code];

		if (!cnt)
			continue;

		for (i = 0; i < KBMON_TOP_KEYS; i++) {
			if (cnt <= top_counts[i])
				continue;

			for (j = KBMON_TOP_KEYS - 1; j > i; j--) {
				top_counts[j] = top_counts[j - 1];
				top_codes[j] = top_codes[j - 1];
			}
			top_counts[i] = cnt;
			top_codes[i] = code;

			if (n < KBMON_TOP_KEYS)
				n++;
			break;
		}
	}

	*top_count_out = n;
}

static int build_session_start_json(const char *host, int interval_sec,
				    char *json, size_t cap)
{
	time_t now = time(NULL);
	size_t len = 0;

	if (appendf(json, cap, &len,
		    "{\"schema\":\"kbmonitor.stats.v1\","
		    "\"type\":\"session_start\","
		    "\"host\":") < 0)
		return -1;
	if (append_json_string(json, cap, &len, host) < 0)
		return -1;
	if (appendf(json, cap, &len,
		    ",\"unix_time\":%lld"
		    ",\"interval_sec\":%d"
		    ",\"privacy\":{"
		    "\"exports_individual_keys\":false,"
		    "\"exports_text\":false,"
		    "\"exports_statistics\":true"
		    "}}\n",
		    (long long)now, interval_sec) < 0)
		return -1;

	return 0;
}

static int build_stats_json(const struct kb_stats *stats, const char *host,
			    int sample, int interval_sec,
			    char *json, size_t cap)
{
	unsigned int top_codes[KBMON_TOP_KEYS] = { 0 };
	int top_n = 0;
	time_t now = time(NULL);
	size_t len = 0;
	int first;
	int i;
	unsigned int code;

	compute_top_keys(stats, top_codes, &top_n);

	if (appendf(json, cap, &len,
		    "{\"schema\":\"kbmonitor.stats.v1\","
		    "\"type\":\"stats_snapshot\","
		    "\"host\":") < 0)
		return -1;
	if (append_json_string(json, cap, &len, host) < 0)
		return -1;
	if (appendf(json, cap, &len,
		    ",\"unix_time\":%lld"
		    ",\"sample\":%d"
		    ",\"interval_sec\":%d"
		    ",\"summary\":{"
		    "\"total_presses\":%llu"
		    ",\"active_keyboards\":%d"
		    ",\"uptime_ms\":%llu"
		    ",\"presses_per_minute\":%llu"
		    ",\"presses_last_10s\":%llu"
		    ",\"peak_presses_per_second\":%llu"
		    ",\"repeat_events\":%llu"
		    ",\"buffer_dropped\":%llu"
		    "},\"categories\":{"
		    "\"letters\":%llu"
		    ",\"digits\":%llu"
		    ",\"modifiers\":%llu"
		    ",\"navigation\":%llu"
		    ",\"function_keys\":%llu"
		    ",\"control_keys\":%llu"
		    ",\"other_keys\":%llu"
		    "},\"top_keys\":[",
		    (long long)now, sample, interval_sec,
		    stats->total_presses,
		    stats->active_keyboards,
		    stats->uptime_ms,
		    stats->presses_per_minute,
		    stats->presses_last_10s,
		    stats->peak_presses_per_second,
		    stats->repeat_events,
		    stats->buffer_dropped,
		    stats->letters,
		    stats->digits,
		    stats->modifiers,
		    stats->navigation,
		    stats->function_keys,
		    stats->control_keys,
		    stats->other_keys) < 0)
		return -1;

	for (i = 0; i < top_n; i++) {
		code = top_codes[i];
		const char *label = stats->labels[code][0] ?
			stats->labels[code] : "UNKNOWN";

		if (appendf(json, cap, &len, "%s{\"key\":",
			    i == 0 ? "" : ",") < 0)
			return -1;
		if (append_json_string(json, cap, &len, label) < 0)
			return -1;
		if (appendf(json, cap, &len, ",\"code\":%u,\"count\":%llu}",
			    code, stats->counts[code]) < 0)
			return -1;
	}

	if (appendf(json, cap, &len, "],\"per_key\":[") < 0)
		return -1;

	first = 1;
	for (code = 0; code < KBMON_KEY_COUNT; code++) {
		const char *label;

		if (!stats->counts[code])
			continue;

		label = stats->labels[code][0] ? stats->labels[code] : "UNKNOWN";

		if (appendf(json, cap, &len, "%s{\"key\":",
			    first ? "" : ",") < 0)
			return -1;
		if (append_json_string(json, cap, &len, label) < 0)
			return -1;
		if (appendf(json, cap, &len, ",\"code\":%u,\"count\":%llu}",
			    code, stats->counts[code]) < 0)
			return -1;
		first = 0;
	}

	return appendf(json, cap, &len, "]}\n");
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

static int tls_client_connect(const struct options *opts,
			      struct tls_client *client)
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
				"warning: peer verification disabled; use --ca-file or --insecure\n");
	}

	if (opts->client_cert && opts->client_key) {
		if (SSL_CTX_use_certificate_file(ctx, opts->client_cert,
						 SSL_FILETYPE_PEM) != 1) {
			fprintf(stderr, "failed to load client cert: %s\n",
				opts->client_cert);
			ERR_print_errors_fp(stderr);
			goto out_ctx;
		}
		if (SSL_CTX_use_PrivateKey_file(ctx, opts->client_key,
						SSL_FILETYPE_PEM) != 1) {
			fprintf(stderr, "failed to load client key: %s\n",
				opts->client_key);
			ERR_print_errors_fp(stderr);
			goto out_ctx;
		}
		fprintf(stderr, "info: mutual TLS enabled (client cert: %s)\n",
			opts->client_cert);
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
	if (errno || end == value || *end != '\0' || parsed < 0 || parsed > 86400)
		return fallback;

	return (int)parsed;
}

static int parse_args(int argc, char **argv, struct options *opts)
{
	int i;

	memset(opts, 0, sizeof(*opts));
	opts->interval_sec = 5;
	opts->count = 0;

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
		else if (!strcmp(argv[i], "--client-cert") && i + 1 < argc)
			opts->client_cert = argv[++i];
		else if (!strcmp(argv[i], "--client-key") && i + 1 < argc)
			opts->client_key = argv[++i];
		else if (!strcmp(argv[i], "--server-name") && i + 1 < argc)
			opts->server_name = argv[++i];
		else if (!strcmp(argv[i], "--insecure"))
			opts->insecure = 1;
		else
			return -1;
	}

	if (opts->interval_sec < 1)
		opts->interval_sec = 1;

	/* auto-detect certs from default paths only when not using --insecure */
	if (!opts->insecure) {
		if (!opts->ca_file && access(DEFAULT_CA_FILE, R_OK) == 0)
			opts->ca_file = DEFAULT_CA_FILE;
		if (!opts->client_cert && access(DEFAULT_CLIENT_CERT, R_OK) == 0)
			opts->client_cert = DEFAULT_CLIENT_CERT;
		if (!opts->client_key && access(DEFAULT_CLIENT_KEY, R_OK) == 0)
			opts->client_key = DEFAULT_CLIENT_KEY;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct options opts;
	struct tls_client client;
	struct kb_stats stats;
	static char json[JSON_BUF_SIZE];
	char hostname[128] = "unknown";
	int sample = 0;

	if (argc >= 2 && (!strcmp(argv[1], "help") ||
			  !strcmp(argv[1], "--help") ||
			  !strcmp(argv[1], "-h"))) {
		usage(argv[0]);
		return 0;
	}

	if (parse_args(argc, argv, &opts) < 0) {
		usage(argv[0]);
		return 1;
	}

	(void)gethostname(hostname, sizeof(hostname));
	hostname[sizeof(hostname) - 1] = '\0';

	SSL_library_init();
	SSL_load_error_strings();

	if (tls_client_connect(&opts, &client) < 0)
		return 1;

	if (build_session_start_json(hostname, opts.interval_sec, json,
				     sizeof(json)) < 0) {
		fprintf(stderr, "failed to build session start JSON\n");
		tls_client_close(&client);
		return 1;
	}

	if (send_tls_payload(&client, json) < 0) {
		tls_client_close(&client);
		return 1;
	}

	printf("sending keyboard statistics to %s:%s every %d second(s)\n",
	       opts.host, opts.port, opts.interval_sec);
	fflush(stdout);

	while (opts.count == 0 || sample < opts.count) {
		sleep((unsigned int)opts.interval_sec);

		if (collect_stats(&stats) < 0) {
			tls_client_close(&client);
			return 1;
		}

		sample++;

		if (build_stats_json(&stats, hostname, sample, opts.interval_sec,
				     json, sizeof(json)) < 0) {
			fprintf(stderr, "failed to build stats JSON\n");
			tls_client_close(&client);
			return 1;
		}

		if (send_tls_payload(&client, json) < 0) {
			tls_client_close(&client);
			return 1;
		}

		printf("sent sample %d: total=%llu rate=%llu/min last10s=%llu\n",
		       sample, stats.total_presses, stats.presses_per_minute,
		       stats.presses_last_10s);
		fflush(stdout);
	}

	tls_client_close(&client);
	return 0;
}
