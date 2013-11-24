// -*- coding: utf-8 -*-
// <nginx markdown handler module - serve markdown files as HTML>
// Copyright (C) <2013> Jean Baptiste Favre <webmaster@jbfavre.org>

//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <mkdio.h>
#include <string.h>

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_log.h>

// We'll need this for markdown convertion
#define MKD_FLAGS MKD_TOC | MKD_AUTOLINK | MKD_TABSTOP | MKD_EXTRA_FOOTNOTE

// Define response header to set a single point of modification
#define RH_HTML_NOT_UTF8 "text/html"
#define RH_HTML_UTF8 "text/html; charset=\"UTF-8\""
#define RH_TEXT_NOT_UTF8 "text/plain"
#define RH_TEXT_UTF8 "text/plain; charset=\"UTF-8\""

static char *
ngx_http_mdhandler_init(ngx_conf_t * cf, ngx_command_t * cmd, void * conf);

static void* ngx_http_mdhandler_create_conf(ngx_conf_t *cf);
static char* ngx_http_mdhandler_merge_conf(ngx_conf_t *cf,void *parent, void *child);

static ngx_int_t
ngx_http_mdhandler_handler(ngx_http_request_t *r);

typedef struct {
    ngx_flag_t enable;
    ngx_flag_t mdh_utf8;
    ngx_str_t mdh_output;
} ngx_http_mdhandler_conf_t;

static ngx_command_t  ngx_http_mdhandler_commands[] = {
    { ngx_string("mdhandler"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_mdhandler_conf_t, enable),
      NULL },

    { ngx_string("mdhandler-output"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_mdhandler_conf_t, mdh_output),
      NULL },

    { ngx_string("mdhandler-utf8"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_mdhandler_conf_t, mdh_utf8),
      NULL },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_mdhandler_module_ctx = {
    NULL,                               /* preconfiguration */
    ngx_http_mdhandler_init,             /* postconfiguration */
    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */
    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */
    ngx_http_mdhandler_create_conf,      /* create location configuration */
    ngx_http_mdhandler_merge_conf /* merge location configuration */
};

ngx_module_t  ngx_http_mdhandler_module = {
    NGX_MODULE_V1,
    &ngx_http_mdhandler_module_ctx, /* module context */
    ngx_http_mdhandler_commands,    /* module directives */
    NGX_HTTP_MODULE,                /* module type */
    NULL,                           /* init master */
    NULL,                           /* init module */
    NULL,                           /* init process */
    NULL,                           /* init thread */
    NULL,                           /* exit thread */
    NULL,                           /* exit process */
    NULL,                           /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_mdhandler_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_buf_t                 *b;
    ngx_chain_t                out;
    ngx_http_mdhandler_conf_t  *conf;

    char                      *format;
    ngx_str_t                  path;
    u_char                    *last;
    size_t                     root;

    FILE                      *md_file;
    MMIOT                     *mkd;
    char                      *html_content;
    int                        html_size=0;

    // r should be set, but why don't test it
    // If we miss it, no need to go
    if (NULL == r) {
        return NGX_ERROR;
    }

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler starts");

    conf = ngx_http_get_module_loc_conf(r, ngx_http_mdhandler_module);
    format = (char *)conf->mdh_output.data;

    // only supports GET & HEAD methods
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_DECLINED;
    }
    // no need for request body since we don't handle POST
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler process md");

    if (!0 == strncmp(format, "html", sizeof("html"))) {

        // set response headers
        if (conf->mdh_utf8) {
            r->headers_out.content_type_len = sizeof(RH_TEXT_UTF8) - 1;
            r->headers_out.content_type.len = r->headers_out.content_type_len;
            r->headers_out.content_type.data = (u_char *) RH_TEXT_UTF8;
        }else{
            r->headers_out.content_type_len = sizeof(RH_TEXT_NOT_UTF8) - 1;
            r->headers_out.content_type.len = r->headers_out.content_type_len;
            r->headers_out.content_type.data = (u_char *) RH_TEXT_NOT_UTF8;
        }
        // and decline request handling
        // so that nginx take care of the remaining stuff
        // yes, I'm lazy
        return NGX_DECLINED;
    }else{

        // set response headers
        if (conf->mdh_utf8) {
            r->headers_out.content_type_len = sizeof(RH_HTML_UTF8) - 1;
            r->headers_out.content_type.len = r->headers_out.content_type_len;
            r->headers_out.content_type.data = (u_char *) RH_HTML_UTF8;
        }else{
            r->headers_out.content_type_len = sizeof(RH_HTML_NOT_UTF8) - 1;
            r->headers_out.content_type.len = r->headers_out.content_type_len;
            r->headers_out.content_type.data = (u_char *) RH_HTML_NOT_UTF8;
        }

        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler open md file [%s]", path.data);

        if (NULL == (last = ngx_http_map_uri_to_path(r, &path, &root, 0))) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        path.len  = last - path.data;

        md_file = fopen((char *)path.data, "r");
        if (!md_file) {
            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "http markdown body filter fail to open [%s]",
                strerror(errno));
            return NGX_HTTP_NOT_FOUND;
        }

        // render as markdown from libdiscount
        mkd = mkd_in(md_file, MKD_FLAGS);
        mkd_compile(mkd, MKD_FLAGS);
        html_size = mkd_document(mkd, &html_content);

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler send headers");

        // send the header only, if the request type is http HEAD
        if (NGX_HTTP_HEAD == r->method) {
            r->headers_out.status = NGX_HTTP_OK;
            r->headers_out.content_length_n = html_size;

            return ngx_http_send_header(r);
        }

        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler create buffer");

        b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
        if (NULL == b) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        out.buf = b;
        out.next = NULL;

        b->pos = (u_char *)html_content;
        b->last = (u_char *)html_content + html_size;
        b-> memory = 1;
        b->last_buf = 1;

        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler send status");

        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n = html_size;

        rc = ngx_http_send_header(r);
        if (NGX_ERROR == rc || rc > NGX_OK || r->header_only) {
            return rc;
        }
        
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler ends");

        return ngx_http_output_filter(r, &out);
    }
}

static char *
ngx_http_mdhandler_init(ngx_conf_t * cf, ngx_command_t * cmd, void * conf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (NULL == h) {
        return NGX_ERROR;
    }

    *h = ngx_http_mdhandler_handler;

    return NGX_OK;
}

static void *
ngx_http_mdhandler_create_conf(ngx_conf_t *cf)
{
    ngx_http_mdhandler_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_mdhandler_conf_t));
    if (NULL == conf) {
        return NGX_CONF_ERROR;
    }
    conf->enable = NGX_CONF_UNSET;
    conf->mdh_utf8 = NGX_CONF_UNSET;

    return conf;
}

static char *
ngx_http_mdhandler_merge_conf(ngx_conf_t *cf, void *parent, void *child){
    ngx_http_mdhandler_conf_t *prev = parent;
    ngx_http_mdhandler_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_value(conf->mdh_utf8, prev->mdh_utf8, 0);
    ngx_conf_merge_str_value(conf->mdh_output, prev->mdh_output, "raw");

    return NGX_CONF_OK;
}
