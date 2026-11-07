# nhttp

### minimal http server

usage: `nhttp DIR` serves DIR on port 80, a request for a directory resolves to its index.html

nhttp must be started as root. before it accepts the first connection it chroots to DIR, drops every supplementary group and switches to the user and group `nhttp`, so the listener and every forked request handler run unprivileged with DIR as their filesystem root. the `nhttp` user has to exist, on gentoo `app-misc/nhttp` pulls in `acct-user/nhttp`

`setcap cap_net_bind_service=+ep` is not enough, the chroot and the user switch need cap_sys_chroot, cap_setuid and cap_setgid. start it as root and let it drop

features:
- GET and HEAD only, everything else 405 with an `Allow: GET, HEAD` header
- chroots to DIR and runs as the `nhttp` user, nothing outside DIR is reachable, through a symlink or otherwise
- listens on port 80, if you need to change it you have to edit and rebuild
- 10 s read/write timeout, if you need to change it you have to edit and rebuild
- the 10 s bound covers the whole request-header phase, not just one read, so a client that dribbles bytes cannot keep a process open
- at most 128 connections are served at once (CONN_MAX), past that new connections wait in the listen backlog instead of forking without limit
- requests whose headers exceed 8192 bytes are rejected with 431
- the body goes out with sendfile(2), the headers and the body share a segment when they fit
- ipv4 only

notes:
- symlinks resolve inside the chroot. a symlink whose target lies outside DIR is dangling and gives 404. an absolute symlink target is resolved with DIR as /
- dotfiles are served like any other file
- `/about` serves `/about/index.html` but does not redirect to `/about/`, so relative links inside it resolve against `/`. use absolute hrefs or trailing slashes. this is deliberate because emitting a `Location` would mean reflecting the request path into a response header
- `%2F` decodes to a real path separator before resolution, `/a%2Fb` and `/a/b` are the same request
- a dir without an index.html returns 404 not 403
- anything that is neither a regular file nor a directory (a fifo, a device, a socket) returns 403
- a request that stalls midway gets its connection dropped when the timeout fires, with no 408 response
- both `\r\n\r\n` and `\n\n` are accepted as the end of the headers
- the response body is only sent when the whole request framed correctly, a truncated request is a 400 rather than a best-effort parse
- children that exit while the server is idle stay as zombies until the next connection arrives. bounded by CONN_MAX and reaped before any new fork, so capacity is unaffected
- no Range support, so no seeking in large media
- while all 128 slots are busy the listener stops accepting until one frees. this is backpressure not failure. every slot is time bounded so it always recovers
- the user name, the port, the timeout, the header cap and the connection limit are the defines at the top of src/main.c

if you have any questions contact me: nhttp@nburch.org
