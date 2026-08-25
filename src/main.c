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
#include <sys/types.h>
#include <unistd.h>

#define PORT       80
#define BACKLOG    64
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
	(void)fd; (void)buf; (void)n;
	// loop on write(), advance a const char * cursor, handle EINTR 
	return -1;
}

static const char *mime_type(const char *path)
{
	(void)path;
	// strrchr(path, '.'), then a table of { ".html", "text/html" }
	return "application/octet-stream";
}


static void send_error(int fd, int code, const char *reason)
{
	(void)fd; (void)code; (void)reason;
}

static int url_decode(const char *src, char *dst, size_t dstsz)
{
	(void)src; (void)dst; (void)dstsz;
	return -1;
}

static int resolve_path(const char *root, const char *urlpath,
                        char *out, size_t outsz)
{
	(void)root; (void)urlpath; (void)out; (void)outsz;
	return RESOLVE_NOTFOUND;
}

static void send_file(int fd, const char *path, int head_only)
{
	(void)fd; (void)path; (void)head_only;
}

static void handle_conn(int cfd, const char *root)
{
	(void)root;
	send_error(cfd, 501, "Not Implemented");
}

static int listen_on(int port)
{
	(void)port;
	return -1;
}

int main(int argc, char **argv)
{
	char root[PATH_MAX];
	int lfd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <docroot>\n", argv[0]);
		return 1;
	}

	// a client closing a tab mid-transfer otherwise kills us outright.
	signal(SIGPIPE, SIG_IGN);
	// auto reap forked children so they don't pile up
	signal(SIGCHLD, SIG_IGN);

	if (!realpath(argv[1], root))
		die(argv[1]);

	lfd = listen_on(PORT);
	fprintf(stderr, "nhttp: serving %s on port %d\n", root, PORT);

	for (;;) {
		int cfd = accept(lfd, NULL, NULL);
		if (cfd < 0) {
			if (errno == EINTR)
				continue;
			die("accept");
		}

		handle_conn(cfd, root);
		close(cfd);
	}
}
