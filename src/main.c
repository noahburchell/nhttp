#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define USER     "nhttp"
#define PORT     80
#define BACKLOG  64
#define TIMEOUT  10
#define CONN_MAX 128
#define REQ_MAX  8192
#define INDEX    "/index.html"

[[noreturn]] static void die(const char* call, const char* obj) {
	fprintf(stderr, "nhttp: %s%s%s: %s\n", call, obj ? " " : "", obj ? obj : "", strerror(errno));
	exit(1);
}

static void usage(FILE* f) {
	fputs("usage: nhttp DIR\n"
	      "\n"
	      "      --help display this help and exit\n"
	      "\n"
	      "DIR is served on port 80 as the document root\n"
	      "must be run as root " USER "\n",
	      f);
}

static int write_all(int fd, const void* buf, size_t n, bool more) {
	const char* p = buf;

	while(n) {
		ssize_t k = send(fd, p, n, more ? MSG_MORE : 0);

		if(k > 0) {
			p += k;
			n -= (size_t)k;
			continue;
		}

		if(k < 0 && errno == EINTR) {
			continue;
		}

		return -1;
	}

	return 0;
}

static const char* mime_type(const char* path) {
	static const char* const tab[][2] = {
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
		{ ".tar",   "application/x-tar" },
		{ ".gz",    "application/gzip" },
		{ ".tgz",   "application/gzip" },
		{ ".xz",    "application/x-xz" },
		{ ".txz",   "application/x-xz" },
		{ ".zip",   "application/zip" },
	};
	const char* ext = strrchr(path, '.');
	size_t i;

	if(ext && !strchr(ext, '/')) {
		for(i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
			if(!strcasecmp(ext, tab[i][0])) {
				return tab[i][1];
			}
		}
	}

	return "application/octet-stream";
}

static void http_date(char* buf, size_t n) {
	time_t t = time(nullptr);
	struct tm tm;

	if(!gmtime_r(&t, &tm) || !strftime(buf, n, "%a, %d %b %Y %H:%M:%S GMT", &tm)) {
		*buf = '\0';
	}
}

static void send_error(int fd, int code, const char* reason, bool head_only) {
	char body[256], head[512], date[64];
	int blen, hlen;

	http_date(date, sizeof(date));
	blen = snprintf(body, sizeof(body), "<!doctype html><title>%d %s</title><h1>%d %s</h1>\n",
	                code, reason, code, reason);
	hlen = snprintf(head, sizeof(head),
	                "HTTP/1.1 %d %s\r\n"
	                "Date: %s\r\n"
	                "Content-Type: text/html; charset=utf-8\r\n"
	                "Content-Length: %d\r\n"
	                "%s"
	                "Connection: close\r\n"
	                "\r\n",
	                code, reason, date, blen, code == 405 ? "Allow: GET, HEAD\r\n" : "");
	if(blen < 0 || (size_t)blen >= sizeof(body) || hlen < 0 || (size_t)hlen >= sizeof(head)) {
		return;
	}

	if(write_all(fd, head, (size_t)hlen, !head_only) == 0 && !head_only) {
		write_all(fd, body, (size_t)blen, false);
	}
}

static int hexval(int c) {
	if(c >= '0' && c <= '9') {
		return c - '0';
	}

	if(c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}

	if(c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}

	return -1;
}

// the decoded length, -1 for a bad escape, an encoded nul or a path that does not fit
static int url_decode(const char* src, char* dst, size_t dstsz) {
	size_t i = 0;

	for(; *src && *src != '?'; src++) {
		int c = (unsigned char)*src;

		if(c == '%') {
			int hi = hexval((unsigned char)src[1]);
			int lo = hi < 0 ? -1 : hexval((unsigned char)src[2]);

			if(lo < 0) {
				return -1;
			}

			c = hi << 4 | lo;
			src += 2;
		}

		if(!c || i + 1 >= dstsz) {
			return -1;
		}

		dst[i++] = (char)c;
	}

	dst[i] = '\0';

	return (int)i;
}

// the regular file PATH names, index.html for a directory, PATH has room for the suffix
static int open_doc(char* path, size_t len, struct stat* st) {
	bool indexed = false;

	for(;;) {
		int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);

		if(fd < 0) {
			return -1;
		}

		if(fstat(fd, st) < 0) {
			close(fd);
			return -1;
		}

		if(S_ISREG(st->st_mode)) {
			return fd;
		}

		close(fd);
		if(!S_ISDIR(st->st_mode) || indexed) {
			errno = EACCES;
			return -1;
		}

		memcpy(path + len, INDEX, sizeof(INDEX));
		len += sizeof(INDEX) - 1;
		indexed = true;
	}
}

static void send_file(int cfd, int fd, const char* path, off_t size, bool head_only) {
	char head[512], date[64];
	off_t off = 0;
	int hlen;

	http_date(date, sizeof(date));
	hlen = snprintf(head, sizeof(head),
	                "HTTP/1.1 200 OK\r\n"
	                "Date: %s\r\n"
	                "Content-Type: %s\r\n"
	                "Content-Length: %lld\r\n"
	                "Connection: close\r\n"
	                "\r\n",
	                date, mime_type(path), (long long)size);
	if(hlen < 0 || (size_t)hlen >= sizeof(head)) {
		send_error(cfd, 500, "Internal Server Error", head_only);
		return;
	}

	if(write_all(cfd, head, (size_t)hlen, !head_only) < 0 || head_only) {
		return;
	}

	while(off < size) {
		ssize_t k = sendfile(cfd, fd, &off, (size_t)(size - off));

		if(k < 0 && errno == EINTR) {
			continue;
		}

		if(k <= 0) {
			return;
		}
	}
}

static void handle_conn(int cfd) {
	char req[REQ_MAX], path[REQ_MAX + sizeof(INDEX)];
	char* target;
	char* sp;
	struct stat st;
	size_t len = 0;
	bool head_only, gone;
	int n, fd;

	alarm(TIMEOUT);
	for(;;) {
		ssize_t r;

		if(len == sizeof(req) - 1) {
			send_error(cfd, 431, "Request Header Fields Too Large", false);
			return;
		}

		r = read(cfd, req + len, sizeof(req) - 1 - len);
		if(r < 0 && errno == EINTR) {
			continue;
		}

		if(r < 0) {
			return;
		}

		if(!r) {
			if(len) {
				send_error(cfd, 400, "Bad Request", false);
			}

			return;
		}

		len += (size_t)r;
		req[len] = '\0';
		if(strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) {
			break;
		}
	}

	alarm(0);
	target = strchr(req, ' ');
	sp = target ? strchr(target + 1, ' ') : nullptr;
	if(!sp) {
		send_error(cfd, 400, "Bad Request", false);
		return;
	}

	*target++ = '\0';
	*sp = '\0';
	head_only = !strcmp(req, "HEAD");
	if(!head_only && strcmp(req, "GET")) {
		send_error(cfd, 405, "Method Not Allowed", false);
		return;
	}

	n = *target == '/' ? url_decode(target, path, REQ_MAX) : -1;
	if(n < 0) {
		send_error(cfd, 400, "Bad Request", head_only);
		return;
	}

	fd = open_doc(path, (size_t)n, &st);
	if(fd < 0) {
		gone = errno == ENOENT || errno == ENOTDIR || errno == ENAMETOOLONG || errno == ELOOP;
		send_error(cfd, gone ? 404 : 403, gone ? "Not Found" : "Forbidden", head_only);
		return;
	}

	send_file(cfd, fd, path, st.st_size, head_only);
	close(fd);
}

static void close_conn(int fd) {
	char buf[4096];

	alarm(TIMEOUT);
	shutdown(fd, SHUT_WR);
	while(read(fd, buf, sizeof(buf)) > 0)
		;
	close(fd);
}

static int reap(int nchild, bool block) {
	while(nchild > 0) {
		pid_t p = waitpid(-1, nullptr, block ? 0 : WNOHANG);

		if(p < 0 && errno == ECHILD) {
			return 0;
		}

		if(p <= 0) {
			break;
		}

		nchild--;
		block = false;
	}

	return nchild;
}

static int listen_on(uint16_t port) {
	struct sockaddr_in addr = { 0 };
	int fd, on = 1;

	fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if(fd < 0) {
		die("socket", nullptr);
	}

	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		die("setsockopt", nullptr);
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		die("bind", nullptr);
	}

	if(listen(fd, BACKLOG) < 0) {
		die("listen", nullptr);
	}

	return fd;
}

int main(int argc, char** argv) {
	struct timeval tv = { TIMEOUT, 0 };
	char root[PATH_MAX];
	struct passwd* pw;
	int lfd, nchild = 0;

	if(argc > 1 && !strcmp(argv[1], "--help")) {
		usage(stdout);
		return 0;
	}

	if(argc > 1 && argv[1][0] == '-' && argv[1][1]) {
		fprintf(stderr, "nhttp: unrecognized option '%s'\n", argv[1]);
		usage(stderr);
		return 2;
	}

	if(argc != 2) {
		fprintf(stderr, argc < 2 ? "nhttp: missing operand\n" : "nhttp: extra operand '%s'\n", argv[2]);
		usage(stderr);
		return 2;
	}

	signal(SIGPIPE, SIG_IGN);
	if(!realpath(argv[1], root)) {
		die("realpath", argv[1]);
	}

	// the user lookup reads /etc/passwd, the chroot below takes that away
	errno = 0;
	pw = getpwnam(USER);
	if(!pw) {
		if(errno) {
			die("getpwnam", USER);
		}

		fprintf(stderr, "nhttp: user " USER " does not exist\n");
		return 1;
	}

	if(!pw->pw_uid) {
		fprintf(stderr, "nhttp: user " USER " has uid 0\n");
		return 1;
	}

	lfd = listen_on(PORT);
	if(chroot(root) < 0) {
		die("chroot", root);
	}

	if(chdir("/") < 0) {
		die("chdir", "/");
	}

	if(setgroups(0, nullptr) < 0) {
		die("setgroups", nullptr);
	}

	if(setgid(pw->pw_gid) < 0) {
		die("setgid", nullptr);
	}

	if(setuid(pw->pw_uid) < 0) {
		die("setuid", nullptr);
	}

	fprintf(stderr, "nhttp: serving %s on port %d as %s\n", root, PORT, USER);
	for(;;) {
		int cfd = accept(lfd, nullptr, nullptr);
		pid_t pid;

		if(cfd < 0) {
			if(errno == EINTR || errno == ECONNABORTED) {
				continue;
			}

			if(errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) {
				if(nchild) {
					nchild = reap(nchild, true);
				} else {
					sleep(1);
				}

				continue;
			}

			die("accept", nullptr);
		}

		setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		nchild = reap(nchild, false);
		if(nchild >= CONN_MAX) {
			nchild = reap(nchild, true);
		}

		pid = fork();
		if(pid < 0) {
			send_error(cfd, 503, "Service Unavailable", false);
			close(cfd);
			continue;
		}

		if(!pid) {
			close(lfd);
			handle_conn(cfd);
			close_conn(cfd);
			_exit(0);
		}

		nchild++;
		close(cfd);
	}
}
