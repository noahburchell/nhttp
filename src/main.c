#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PORT       80
#define BACKLOG    64
#define TIMEOUT    10
#define REQ_MAX    8192
#define IO_BUF     65536

#define RESOLVE_OK        0
#define RESOLVE_FORBIDDEN -1
#define RESOLVE_NOTFOUND  -2

static void die(const char *msg)
{
	perror(msg);
	exit(1);
}

static int write_all(int fd, const void *buf, size_t n)
{
	const char *p = buf;

	while (n) {
		ssize_t w = write(fd, p, n);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += w;
		n -= w;
	}
	return 0;
}

static const char *mime_type(const char *path)
{
	static const char *const tab[][2] = {
		{ ".html",  "text/html; charset=utf-8" },
		{ ".htm",   "text/html; charset=utf-8" },
		{ ".css",   "text/css; charset=utf-8" },
		{ ".js",    "text/javascript; charset=utf-8" },
		{ ".json",  "application/json" },
		{ ".txt",   "text/plain; charset=utf-8" },
		{ ".svg",   "image/svg+xml" },
		{ ".png",   "image/png" },
		{ ".jpg",   "image/jpeg" },
		{ ".jpeg",  "image/jpeg" },
		{ ".gif",   "image/gif" },
		{ ".webp",  "image/webp" },
		{ ".ico",   "image/x-icon" },
		{ ".woff2", "font/woff2" },
		{ ".pdf",   "application/pdf" },
	};
	const char *base = strrchr(path, '/');
	const char *ext = strrchr(base ? base : path, '.');
	size_t i;

	if (ext)
		for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
			if (!strcasecmp(ext, tab[i][0]))
				return tab[i][1];
	return "application/octet-stream";
}

static void http_date(char *buf, size_t n)
{
	time_t t = time(NULL);
	struct tm tm;

	strftime(buf, n, "%a, %d %b %Y %H:%M:%S GMT", gmtime_r(&t, &tm));
}

static void send_error(int fd, int code, const char *reason)
{
	char body[256], head[512], date[32];
	int blen, hlen;

	http_date(date, sizeof(date));
	blen = snprintf(body, sizeof(body),
		"<!doctype html><title>%d %s</title><h1>%d %s</h1>\n",
		code, reason, code, reason);
	hlen = snprintf(head, sizeof(head),
		"HTTP/1.1 %d %s\r\n"
		"Date: %s\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		code, reason, date, blen);

	if (write_all(fd, head, hlen) == 0)
		write_all(fd, body, blen);
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int url_decode(const char *src, char *dst, size_t dstsz)
{
	size_t i = 0;

	for (; *src && *src != '?'; src++) {
		int c = (unsigned char)*src;

		if (c == '%') {
			int hi = hexval((unsigned char)src[1]);
			int lo = hi < 0 ? -1 : hexval((unsigned char)src[2]);
			if (lo < 0)
				return -1;
			c = hi << 4 | lo;
			src += 2;
		}
		if (c == '\0' || i + 1 >= dstsz)
			return -1;
		dst[i++] = c;
	}
	dst[i] = '\0';
	return 0;
}

static int resolve_path(const char *root, const char *urlpath,
                        char *out, size_t outsz)
{
	char joined[PATH_MAX], real[PATH_MAX];
	size_t rootlen = strcmp(root, "/") ? strlen(root) : 0;
	struct stat st;
	int n, pass;

	if (urlpath[0] != '/')
		return RESOLVE_FORBIDDEN;

	n = snprintf(joined, sizeof(joined), "%.*s%s", (int)rootlen, root, urlpath);
	if (n < 0 || (size_t)n >= sizeof(joined))
		return RESOLVE_FORBIDDEN;

	for (pass = 0; pass < 2; pass++) {
		if (!realpath(joined, real))
			return RESOLVE_NOTFOUND;
		if (strncmp(real, root, rootlen) ||
		    (real[rootlen] && real[rootlen] != '/'))
			return RESOLVE_FORBIDDEN;
		if (stat(real, &st) < 0)
			return RESOLVE_NOTFOUND;
		if (S_ISREG(st.st_mode))
			break;
		if (!S_ISDIR(st.st_mode) || pass)
			return RESOLVE_FORBIDDEN;
		n = snprintf(joined, sizeof(joined), "%s/index.html", real);
		if (n < 0 || (size_t)n >= sizeof(joined))
			return RESOLVE_FORBIDDEN;
	}

	if ((size_t)snprintf(out, outsz, "%s", real) >= outsz)
		return RESOLVE_FORBIDDEN;
	return RESOLVE_OK;
}

static void send_file(int fd, const char *path, int head_only)
{
	char buf[IO_BUF], head[512], date[32];
	struct stat st;
	off_t left;
	ssize_t r;
	int ffd, hlen;

	ffd = open(path, O_RDONLY);
	if (ffd < 0) {
		send_error(fd, 403, "Forbidden");
		return;
	}
	if (fstat(ffd, &st) < 0 || !S_ISREG(st.st_mode)) {
		close(ffd);
		send_error(fd, 403, "Forbidden");
		return;
	}

	http_date(date, sizeof(date));
	hlen = snprintf(head, sizeof(head),
		"HTTP/1.1 200 OK\r\n"
		"Date: %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %lld\r\n"
		"Connection: close\r\n"
		"\r\n",
		date, mime_type(path), (long long)st.st_size);

	if (write_all(fd, head, hlen) == 0 && !head_only) {
		left = st.st_size;
		while (left > 0 && (r = read(ffd, buf, sizeof(buf))) > 0) {
			if (r > left)
				r = left;
			if (write_all(fd, buf, r) < 0)
				break;
			left -= r;
		}
	}

	close(ffd);
}

static void handle_conn(int cfd, const char *root)
{
	char req[REQ_MAX], path[PATH_MAX], file[PATH_MAX];
	char *method, *target, *sp;
	size_t len = 0;
	ssize_t r;
	int head_only;

	while (len < sizeof(req) - 1) {
		r = read(cfd, req + len, sizeof(req) - 1 - len);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return;
		len += r;
		req[len] = '\0';
		if (strstr(req, "\r\n\r\n"))
			break;
	}
	req[len] = '\0';

	method = req;
	sp = strchr(req, ' ');
	if (!sp) {
		send_error(cfd, 400, "Bad Request");
		return;
	}
	*sp = '\0';
	target = sp + 1;
	sp = strchr(target, ' ');
	if (!sp) {
		send_error(cfd, 400, "Bad Request");
		return;
	}
	*sp = '\0';

	head_only = !strcmp(method, "HEAD");
	if (!head_only && strcmp(method, "GET")) {
		send_error(cfd, 405, "Method Not Allowed");
		return;
	}

	if (url_decode(target, path, sizeof(path)) < 0) {
		send_error(cfd, 400, "Bad Request");
		return;
	}

	switch (resolve_path(root, path, file, sizeof(file))) {
	case RESOLVE_OK:
		send_file(cfd, file, head_only);
		break;
	case RESOLVE_FORBIDDEN:
		send_error(cfd, 403, "Forbidden");
		break;
	default:
		send_error(cfd, 404, "Not Found");
	}
}

static void close_conn(int fd)
{
	char buf[4096];

	shutdown(fd, SHUT_WR);
	while (read(fd, buf, sizeof(buf)) > 0)
		;
	close(fd);
}

static int listen_on(int port)
{
	struct sockaddr_in addr;
	int fd, on = 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		die("socket");
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		die("setsockopt");

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		die("bind");
	if (listen(fd, BACKLOG) < 0)
		die("listen");

	return fd;
}

int main(int argc, char **argv)
{
	struct timeval tv = { TIMEOUT, 0 };
	char root[PATH_MAX];
	int lfd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <docroot>\n", argv[0]);
		return 1;
	}

	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	if (!realpath(argv[1], root))
		die(argv[1]);

	lfd = listen_on(PORT);
	fprintf(stderr, "nhttp: serving %s on port %d\n", root, PORT);

	for (;;) {
		int cfd = accept(lfd, NULL, NULL);
		pid_t pid;

		if (cfd < 0) {
			if (errno == EINTR || errno == ECONNABORTED)
				continue;
			die("accept");
		}

		setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

		pid = fork();
		if (pid < 0) {
			send_error(cfd, 503, "Service Unavailable");
			close_conn(cfd);
			continue;
		}
		if (pid == 0) {
			close(lfd);
			handle_conn(cfd, root);
			close_conn(cfd);
			_exit(0);
		}
		close(cfd);
	}
}
