// -*- coding: utf-8 -*-
// <nginx markdown filter module - serve markdown files as HTML>
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
#define RH_UTF8 "text/html; charset=\"UTF-8\""
#define RH_NOT_UTF8 "text/html"

// Initialisation function
static ngx_int_t ngx_http_markdown_filter_init(ngx_conf_t *cf);
// Configuration management functions
static void* ngx_http_markdown_filter_create_conf(ngx_conf_t *cf);
static char* ngx_http_markdown_filter_merge_conf(ngx_conf_t *cf,void *parent, void *child);
// Filtering function on response headers
//static ngx_int_t ngx_http_markdown_header_filter(ngx_http_request_t *r, ngx_chain_t *in);
// Filtering function on response body
//static ngx_int_t ngx_http_markdown_body_filter(ngx_http_request_t *r, ngx_chain_t *in);

// Pointers to find next header/body filter
static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt ngx_http_next_body_filter;

// Store configuration options
typedef struct {
    ngx_flag_t enable;
    ngx_flag_t mdf_utf8;
} ngx_http_markdown_filter_conf_t;

// Store module context
typedef struct {
    MMIOT                  *doc;
    char                    content;
    char                    html_content;
    ngx_http_request_t     *request;
    ngx_uint_t              done;         /* unsigned  done:1; */
} ngx_http_markdown_filter_ctx_t;

// Configuration option's list
// MUST end with ngx_null_command
static ngx_command_t  ngx_http_markdown_filter_commands[] = {
    { ngx_string("mdfilter"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_markdown_filter_conf_t, enable),
      NULL },

    { ngx_string("mdfilter-utf8"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_markdown_filter_conf_t, mdf_utf8),
      NULL },

      ngx_null_command
};

// Plug module's functions on nginx hooks
static ngx_http_module_t  ngx_http_markdown_filter_module_ctx = {
    NULL,                               /* preconfiguration */
    ngx_http_markdown_filter_init,      /* postconfiguration */
    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */
    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */
    ngx_http_markdown_filter_create_conf, /* create location configuration */
    ngx_http_markdown_filter_merge_conf   /* merge location configuration */
};

ngx_module_t  ngx_http_markdown_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_markdown_filter_module_ctx, /* module context */
    ngx_http_markdown_filter_commands,    /* module directives */
    NGX_HTTP_MODULE,                      /* module type */
    NULL,                                 /* init master */
    NULL,                                 /* init module */
    NULL,                                 /* init process */
    NULL,                                 /* init thread */
    NULL,                                 /* exit thread */
    NULL,                                 /* exit process */
    NULL,                                 /* exit master */
    NGX_MODULE_V1_PADDING
};

// Function to filter response's header
static ngx_int_t
ngx_http_markdown_header_filter(ngx_http_request_t *r)
{
    ngx_http_markdown_filter_conf_t *conf;

    // Get module configuration
    conf = ngx_http_get_module_loc_conf(r, ngx_http_markdown_filter_module);
    // If module not enabled, call next filter.
    if (!conf->enable) {
        return ngx_http_next_header_filter(r);
    }
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown header filter starts");

    // set new response headers
    if (conf->mdf_utf8) {
        r->headers_out.content_type_len = sizeof(RH_UTF8) - 1;
        r->headers_out.content_type.len = r->headers_out.content_type_len;
        r->headers_out.content_type.data = (u_char *) RH_UTF8;
    }else{
        r->headers_out.content_type_len = sizeof(RH_NOT_UTF8) - 1;
        r->headers_out.content_type.len = r->headers_out.content_type_len;
        r->headers_out.content_type.data = (u_char *) RH_NOT_UTF8;
    }
    ngx_http_clear_content_length(r);
    ngx_http_clear_accept_ranges(r);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown header filter ends");
    return ngx_http_next_header_filter(r);
}

// Function to filter body's header
static ngx_int_t
ngx_http_markdown_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{

    ngx_buf_t                       *b;
    ngx_uint_t                       last=0;
    ngx_chain_t                     *cl;
    ngx_http_markdown_filter_conf_t *conf;

    FILE                            *md_file;
    MMIOT                           *mkd;
    char                            *html_content;
    int                              html_size=0;

    // r and in should be set, but why don't test it
    // If we miss on of them, no need to go
    if (NULL == r || NULL == in) {
        return NGX_ERROR;
    }

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown body filter starts");

    // Get module configuration
    conf = ngx_http_get_module_loc_conf(r, ngx_http_markdown_filter_module);
    // If module not enabled, call next filter.
    if (!conf->enable) {
        return ngx_http_next_body_filter(r, in);
    }

    // Don't know exactly wht's going on here, but hey, it works :-/
    for (cl = in; cl; cl = cl->next) {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "http markdown body filter buf t:%d f:%d, "
                "start: %p, pos: %p, size: %z "
                "file_pos: %O, file_size: %z",
                cl->buf->temporary, cl->buf->in_file,
                cl->buf->start, cl->buf->pos,
                cl->buf->last - cl->buf->pos,
                cl->buf->file_pos,
                cl->buf->file_last - cl->buf->file_pos);
        if (cl->buf->last_buf) {
            last = 1;
            break;
        }
    }

    if (!last)
        return ngx_http_next_body_filter(r, in);

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http markdown body filter creating new buffer");
    b = ngx_calloc_buf(r->pool);
    // If no buffer, then we have a problem.
    if (NULL == b) {
        return NGX_ERROR;
    }

    // Buffer designate a file.
    if(1 == cl->buf->in_file) {
        // Open File
        // TODO: use ngx_ primitive to use integrated cache ?
        md_file = fdopen(cl->buf->file->fd, "r");
        if (!md_file) {
            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "http markdown body filter file open [%s]",
                strerror(errno));
        }
    
        // render as markdown from discount lib
        mkd = mkd_in(md_file, MKD_FLAGS);
        mkd_compile(mkd, MKD_FLAGS);
        html_size = mkd_document(mkd, &html_content);
    }
    if (html_content) {
        // Build new buffer with html_content from discount lib
        b->pos = (u_char *) html_content;
        b->last = b->pos + html_size - 1;
        b->start = b->pos;
        b->end = b->last;
        b->last_buf = 1;
        b->memory = 1;

        // Replace previous buffer with our own one.
        cl->buf = b;
    }

    return ngx_http_next_body_filter(r, in);
}

// Init function
static ngx_int_t
ngx_http_markdown_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_markdown_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_markdown_body_filter;

    return NGX_OK;
}

// Create module config
static void *
ngx_http_markdown_filter_create_conf(ngx_conf_t *cf)
{
    ngx_http_markdown_filter_conf_t *conf;

    if (NULL == cf) {
        return NGX_CONF_ERROR;
    }

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_markdown_filter_conf_t));
    if (NULL == conf) {
        return NGX_CONF_ERROR;
    }
    
    conf->enable = NGX_CONF_UNSET;
    conf->mdf_utf8 = NGX_CONF_UNSET;

    return conf;
}

// Merge local conf with global one
static char *
ngx_http_markdown_filter_merge_conf(ngx_conf_t *cf, void *parent, void *child){
    if (NULL == parent || NULL == child) {
        return NGX_CONF_ERROR;
    }

    ngx_http_markdown_filter_conf_t *prev = parent;
    ngx_http_markdown_filter_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_value(conf->mdf_utf8, prev->mdf_utf8, 0);

    return NGX_CONF_OK;
}
