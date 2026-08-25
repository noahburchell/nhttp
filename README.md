# nhttp

### minimal http server

usage: `sudo nhttp <docroot>` serves that dir, and any dir path resolve to index.html

if you dont want to run as root every time do this: `sudo setcap cap_net_bind_service=+ep nhttp`

features:
- GET and HEAD only, everything else 405
- symlinks pointing outside the docroot are refused (403)
- listens on port 80, if you need to change it you have to edit and rebuild
- 10s read/write timeout, if you need to change it you have to edit and rebuild
- ipv4 only

notes:
- dotfiles are served like any other file
- `/about` serves `/about/index.html` but does not redirect to `/about/`, so relative links inside it resolve against `/`. use absolute hrefs or trailing slashes
- headers past 8192 bytes arent rejected with 431, the loop just exits and parses whatevers in the buffer
- `%2F` decodes to a real path separator before resolution, `/a%2Fb` and `/a/b` are the same request
- a dir without an index.html returns 404 not 403
- no privilege dropping, forks are root

if you have any questions contact me: nhttp@nburch.org 