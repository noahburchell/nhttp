# nhttp

### minimal http server

usage: `sudo nhttp <docroot>` serves that dir, and any dir path resolve to index.html

if you dont want to run as root every time do this: `sudo setcap cap_net_bind_service=+ep nhttp`
that is the recommended way to run it, there is no privilege dropping so running under
`sudo` means every forked child is root too

features:
- GET and HEAD only, everything else 405 with an `Allow: GET, HEAD` header
- symlinks pointing outside the docroot are refused (403)
- listens on port 80, if you need to change it you have to edit and rebuild
- 10s read/write timeout, if you need to change it you have to edit and rebuild
- the 10s bound covers the whole request-header phase, not just one read, so a client that dribbles bytes cannot keep a process open
- at most 128 connections are served at once (CONN_MAX), past that new connections wait in the listen backlog instead of forking without limit
- requests whose headers exceed 8192 bytes are rejected with 431
- ipv4 only

notes:
- dotfiles are served like any other file
- `/about` serves `/about/index.html` but does not redirect to `/about/`, so relative links inside it resolve against `/`. use absolute hrefs or trailing slashes. this is deliberate becasue emitting a `Location` would mean reflecting the request path into a response header
- `%2F` decodes to a real path separator before resolution, `/a%2Fb` and `/a/b` are the same request
- a dir without an index.html returns 404 not 403
- no privilege dropping, forks are root
- a request that stalls midway gets its connection dropped when the timeout fires, with no 408 response
- both `\r\n\r\n` and `\n\n` are accepted as the end of the headers
- the response body is only sent when the whole request framed correctly, a truncated request is a 400 rather than a best-effort parse
- children that exit while the server is idle stay as zombies until the next connection arrives. bounded by CONN_MAX and reaped before any new fork, so capacity is unaffected
- no Range support, so no seeking in large media
- while all 128 slots are busy the listener stops accepting until one frees. this is backpressure not failure. every slot is time bounded so it always recovers

if you have any questions contact me: nhttp@nburch.org
