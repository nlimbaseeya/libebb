/* This file is part of the libebb web server library
 *
 * Copyright (c) 2008 Ryan Dahl (ry@tinyclouds.org)
 * All rights reserved.
 *
 * This parser is based on code from Zed Shaw's Mongrel.
 * Copyright (c) 2005 Zed A. Shaw
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
 */
#ifndef ebb_request_parser_h
#define ebb_request_parser_h

#include <sys/types.h> 

typedef struct ebb_request ebb_request;
typedef struct ebb_request_parser  ebb_request_parser;
typedef void (*ebb_header_cb)(ebb_request*, const char *at, size_t length, int header_index);
typedef void (*ebb_element_cb)(ebb_request*, const char *at, size_t length);

#define EBB_MAX_MULTIPART_BOUNDARY_LEN 20

struct ebb_request {
  enum { EBB_COPY
       , EBB_DELETE
       , EBB_GET
       , EBB_HEAD
       , EBB_LOCK
       , EBB_MKCOL
       , EBB_MOVE
       , EBB_OPTIONS
       , EBB_POST
       , EBB_PROPFIND
       , EBB_PROPPATCH
       , EBB_PUT
       , EBB_TRACE
       , EBB_UNLOCK
       } method;
  
  enum { EBB_IDENTITY
       , EBB_CHUNKED
       } transfer_encoding;          /* ro */

  size_t content_length;             /* ro - 0 if unknown */
  size_t body_read;                  /* ro */
  int expect_continue;               /* ro */
  unsigned int version_major;        /* ro */
  unsigned int version_minor;        /* ro */
  int number_of_headers;             /* ro */
  int keep_alive;                    /* private - use ebb_request_should_keep_alive */

  char multipart_boundary[EBB_MAX_MULTIPART_BOUNDARY_LEN]; /* ro */
  unsigned int multipart_boundary_len; /* ro */

  /* Public  - ordered list of callbacks */
  ebb_element_cb on_path;
  ebb_element_cb on_query_string;
  ebb_element_cb on_uri;
  ebb_element_cb on_fragment;
  ebb_header_cb  on_header_field;
  ebb_header_cb  on_header_value;
  void (*on_headers_complete)(ebb_request *);
  ebb_element_cb on_body;
  void (*on_complete)(ebb_request *);
  void *data;
};

struct ebb_request_parser {
  int cs;                           /* private */
  size_t chunk_size;                /* private */
  unsigned eating:1;                /* private */
  ebb_request *current_request;     /* ro */
  const char *header_field_mark; 
  const char *header_value_mark; 
  const char *query_string_mark; 
  const char *path_mark; 
  const char *uri_mark; 
  const char *fragment_mark; 

  /* Public */
  ebb_request* (*new_request)(void*);
  void *data;
};

void ebb_request_parser_init(ebb_request_parser *parser);
size_t ebb_request_parser_execute(ebb_request_parser *parser, const char *data, size_t len);
int ebb_request_parser_has_error(ebb_request_parser *parser);
int ebb_request_parser_is_finished(ebb_request_parser *parser);
void ebb_request_init(ebb_request *);
int ebb_request_should_keep_alive(ebb_request *request);
#define ebb_request_has_body(request) \
  (request->transfer_encoding == EBB_CHUNKED || request->content_length > 0 )

#endif
