# Nginx Markdown module

Allows NGinx to convert files from markdown into html.
It has been developed and tested with Nginx 1.4.3

This is an academic work to learn C and see how to develop an Nginx module.

__It's likely to be insanely insecure, so... **DO NOT USE IT FOR PRODUCTION**.__

You've been warned.

This module uses [discount library](http://www.pell.portland.or.us/~orc/Code/discount/)

The module is available as:

- a filter module (functional)
- a handler module (not yet functional)

Configuration directives are different so you can use both module in parallel
(but do not activate them for the same location).

## License

Both Markdown Handler & Filter Nginx module are released under GPLv3.
You can find the license text in file LICENSE in this repository.

## Install

### On Debian

Install Nginx sources and dependencies.

    apt-get source nginx
    apt-get build-dep nginx

Install discount library development package

    apt-get install libmarkdown2-dev

Configure and build Nginx

    ./configure \
        --add-module path_to_module_dir/ngx-markdown-module/ngx-md-filter-module \
        --add-module path_to_module_dir/ngx-markdown-module/handler
    make
    make install

## Configuration

### Filter

Markdown filter module exports 2 configuration options

| Option        | Values   | Comment                                    |
|---------------|----------|--------------------------------------------|
| mdfilter      | on / off | enable / disable module                    |
| mdfilter-utf8 | on / off | enable / disable UTF8 content-type headers |

### Handler

Markdown handler module exports 2 configuration options

| Option         | Values   | Comment                                    |
|----------------|----------|--------------------------------------------|
| mdhandler      | -        | if present, enable module                  |
| mdhandler-utf8 | on / off | enable / disable UTF8 content-type headers |


### Sample Nginx configuration

    server {
        listen 80 default_server;
        server_name localhost;

        root /var/www;
        index index.html index.htm;

        location / {
            autoindex on;
            try_files $uri $uri/ =404;
        }
        // Use Filter module
        location ~ /*.md {
            mdfilter on;
            mdfilter_utf8 on;
        }
        // Use Handler module
        location ~ /*.mkd {
            mdhandler;
            mdhandler_utf8 on;
        }
    }

## TODO

1. Add sanity checks :(
2. Make use of nginx file cache layer to open files
3. Add sanity checks :-/
4. Make discount flags available as part of configuration
5. Add sanity checks ;)
