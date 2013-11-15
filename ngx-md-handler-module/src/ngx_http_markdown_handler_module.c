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

static char *
ngx_http_markdown(ngx_conf_t * cf, ngx_command_t * cmd, void * conf);

static void* ngx_http_markdown_create_conf(ngx_conf_t *cf);
static char* ngx_http_markdown_merge_conf(ngx_conf_t *cf,void *parent, void *child);

static ngx_int_t
ngx_http_markdown_handler(ngx_http_request_t *r);

static u_char ngx_markdown_string[] = "# MARKDOWN MODULE";

typedef struct {
    ngx_flag_t enable;
    ngx_str_t output;
} ngx_http_markdown_conf_t;

static ngx_command_t  ngx_http_markdown_commands[] = {
    { ngx_string("mdhandler"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_markdown,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("mdhandler-output"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_markdown_conf_t, output),
      NULL },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_markdown_module_ctx = {
    NULL,                               /* preconfiguration */
    NULL,                               /* postconfiguration */
    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */
    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */
    ngx_http_markdown_create_conf,      /* create location configuration */
    ngx_http_markdown_merge_conf /* merge location configuration */
};

ngx_module_t  ngx_http_markdown_module = {
    NGX_MODULE_V1,
    &ngx_http_markdown_module_ctx, /* module context */
    ngx_http_markdown_commands,    /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_markdown_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_buf_t                 *b;
    ngx_chain_t                out;
    ngx_http_markdown_conf_t  *conf;

    char *html_content;
    int html_size;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler starts");

    conf = ngx_http_get_module_loc_conf(r, ngx_http_markdown_module);

    // only supports GET & HEAD methods
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_DECLINED;
    }
    // no need for request body since we don't handle POST
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler set headers");

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler format is \"%s\"", conf->output.data);
    char *format = (char *)conf->output.data;
    if (strcmp(format, "html") == 0) {
        // version 1. Take a hard-coded string & render it as HTML

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler process md");

        int MKD_FLAGS = MKD_AUTOLINK;
        MMIOT *mkd;

        // set response headers
        r->headers_out.content_type_len = sizeof("text/html; charset=\"UTF-8\"") - 1;
        r->headers_out.content_type.len = sizeof("text/html; charset=\"UTF-8\"") - 1;
        r->headers_out.content_type.data = (u_char *) "text/html; charset=\"UTF-8\"";

        // render as markdown
        mkd = mkd_string((char *)ngx_markdown_string, sizeof(ngx_markdown_string) - 1, MKD_FLAGS);
        mkd_compile(mkd, MKD_FLAGS);
        html_size = mkd_document(mkd, &html_content);

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler send headers");
    }else{
        // Version 0. Take hard-coded string & render it as is.

        // set response headers
        r->headers_out.content_type_len = sizeof("text/plain") - 1;
        r->headers_out.content_type.len = sizeof("text/plain") - 1;
        r->headers_out.content_type.data = (u_char *) "text/plain";

        // render as is
        html_content = (char *)ngx_markdown_string;
        html_size = sizeof(ngx_markdown_string) - 1;
    }

    // send the header only, if the request type is http HEAD
    if (r->method == NGX_HTTP_HEAD) {
        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n = html_size;

        return ngx_http_send_header(r);
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler create buffer");

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    b->pos = (u_char *)html_content;
    b->last = (u_char *)html_content + html_size;
    b-> memory = 1;
    b->last_buf = 1;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler send status");

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = html_size;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
    
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown handler ends");

    return ngx_http_output_filter(r, &out);
}

static char *
ngx_http_markdown(ngx_conf_t * cf, ngx_command_t * cmd, void * conf)
{

    ngx_http_core_loc_conf_t * clcf;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_markdown_handler;

    // handler to process the mdhandler directive
    return NGX_CONF_OK;
}

static void *
ngx_http_markdown_create_conf(ngx_conf_t *cf)
{
    ngx_http_markdown_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_markdown_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    return conf;
}

static char *
ngx_http_markdown_merge_conf(ngx_conf_t *cf, void *parent, void *child){
    ngx_http_markdown_conf_t *prev = parent;
    ngx_http_markdown_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);

    ngx_conf_merge_str_value(conf->output, prev->output, "raw");

    return NGX_CONF_OK;
}
