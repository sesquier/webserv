*This project has been created as part of the 42 curriculum by sesquier, ale-coss.*

# webserv

## Description

webserv is an HTTP/1.1 server written in C++98. It reads a configuration file
inspired by NGINX, then serves one or more websites from a single process.

Features:

- **Non-blocking I/O**: a single `epoll` loop handles every socket and every
  CGI pipe. No read or write happens without `epoll` readiness, so a slow
  client or a slow script never blocks the server.
- **Several servers**: several `server` blocks, each with its own
  `listen` addresses, `root`, error pages and body size limit.
- **Methods**: `GET`, `POST` (file upload) and `DELETE`.
- **Routing per location**: allowed methods, `root`, `index`, `autoindex`,
  redirections (`return 301 /`), upload directory, body size limit.
- **CGI**: scripts chosen by their extension (`.py`, `.pl`, `.sh`), with the
  RFC 3875 environment. The body is sent on the script's stdin; chunked
  requests are un-chunked first. A script that hangs gets a `504`, and a script
  that crashes gets a `502`.
- **Errors**: accurate status codes (400, 403, 404, 405, 408, 411, 413, 431,
  501…), default error pages and custom ones (`error_page`).
- **Protection**: no path traversal (`..`, `%2e%2e`); timeouts for idle
  clients; uploaded files are never executed.

## Instructions

### Build

```sh
make            # builds ./webserv
make clean      # removes the object files
make fclean     # also removes ./webserv
make re         # rebuilds everything
```

You need a C++ compiler (`c++`) and `make`. The CGI demos use `python3`,
`perl` and `bash`.

### Run

```sh
./webserv                       # uses config/default.conf
./webserv path/to/server.conf   # uses another configuration file
```

With the default configuration:

| URL | What it shows |
|---|---|
| http://localhost:8080/ | site 1, with links to every feature |
| http://localhost:8080/files/ | a directory listing (autoindex) |
| http://localhost:8080/cgi-bin/hello.py?name=Bob | a CGI script |
| http://localhost:8081/ | site 2, on another port |

Examples with `curl`:

```sh
curl -i --data-binary @file.txt http://localhost:8080/upload/file.txt  # upload: 201
curl -i http://localhost:8080/upload/file.txt                          # read it: 200
curl -i -X DELETE http://localhost:8080/upload/file.txt                # delete it: 204
```

### Configuration

```nginx
server {
    listen 127.0.0.1:8080;
    root ./www/site1;
    index index.html;
    client_max_body_size 1M;
    error_page 404 ./www/site1/errors/404.html;

    location / {
        methods GET;
    }

    location /upload {
        root ./www/site1/uploads;
        methods GET POST DELETE;
        upload_store ./www/site1/uploads;
        client_max_body_size 10M;
    }

    location /cgi-bin {
        root ./www/site1/cgi-bin;
        methods GET POST;
        cgi .py /usr/bin/python3;
        cgi .pl /usr/bin/perl;
    }

    location /old {
        return 301 /;
    }
}
```

See `config/default.conf` for a complete example.

### Tests

```sh
tests/run_all.sh              # every test suite (ports 8080, 8081 and 8180-8183 must be free)
tests/run_all.sh --valgrind   # the same, under valgrind: no leak, no open fd left
```

## Resources

- [RFC 9110 – HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110):
  methods, status codes, headers.
- [RFC 9112 – HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112): message
  format, `Content-Length`, chunked transfer coding.
- [RFC 3875 – The Common Gateway Interface](https://www.rfc-editor.org/rfc/rfc3875):
  CGI variables and script output.
- [NGINX documentation](https://nginx.org/en/docs/): the reference for the
  configuration file and for how the server should behave.
- `man epoll`, `man 2 socket`, `man 2 fork`, `man 2 execve`, `man 2 pipe`.
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/):
  sockets in C.

### Use of AI

We used an AI assistant (Claude) for these tasks:

- **Learning**: explanations of HTTP, epoll, non-blocking I/O and CGI, with
  questions to check our understanding before coding.
- **Code**: help factoring our code and organizing our files.
- **Tests**: the Python test scripts in `tests/` and `tests/run_all.sh`, then
  a manual test checklist that we ran ourselves.
- **Debugging**: finding the causes of errors, for example a missing include,
  or a crashed script answered with an empty `200` instead of a `502`.

We reviewed, tested and discussed every part of the project before keeping it.
