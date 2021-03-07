# cc-simplehttpfileserver

This repository contains the `cc-simplehttpfileserver` library.

It is a highly efficient, lightweight, basic http 1.1 fileserver that lives behind a proxy
and can efficiently serve static files using `sendfile` and `epoll`.

It eliminates the requirement to include `nginx` in your stack just to serve static files behind
a load-balancer or other reverse proxy.

## Building

```sh
make
```
