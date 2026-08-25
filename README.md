# nhttp

### minimal http server

usage: `sudo nhttp <docroot>` serves that dir, `/` and any dir path resolve to index.html

if you dont want to run as root every time do this: `sudo setcap cap_net_bind_service=+ep nhttp`

features:
- GET and HEAD only, everything else 405
- symlinks pointing outside the docroot are refused (403)
- listens on port 80, if you need to change it you have to edit and rebuild
- no read timeout, if you need one add it (`setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, ...)` ... )

`/about` serves `/about/index.html` but does not redirect to `/about/`,
so relative links inside it resolve against `/`. use absolute hrefs or
trailing slashes