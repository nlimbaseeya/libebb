#include "ebb.h"
#include "request_parser.h"
#include <ev.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <netinet/in.h>  /* inet_ntoa */
#include <arpa/inet.h>   /* inet_ntoa */
#include <unistd.h>
#include <error.h>
#include <assert.h>

#include <stdio.h>      /* perror */
#include <errno.h>      /* perror */

static const char *expect_continue_response = "100 Continue\r\n\r\n";

static void set_nonblock
  ( int fd
  )
{
  int flags = fcntl(fd, F_GETFL, 0);
  int r = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  assert(0 <= r && "Setting socket non-block failed!");
}

static ebb_request_info* new_request_info
  ( void *data
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = NULL;
  if(connection->new_request) 
    request = connection->new_request(connection);
  if(request == NULL)
    return NULL;

  request->connection = connection; 
  ebb_request *last_request = ebb_connection_current_request(connection);
  assert(last_request->next  == NULL);
  last_request->next = request;
  assert(request == ebb_connection_current_request(connection));

  return &request->info;  
}

static void request_complete
  ( void *data
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = NULL;
  if(connection->new_request) 
    request = ebb_connection_current_request(connection);
  assert(request != NULL);
    
  if(request->on_complete)
    request->on_complete(request);    
}

/* <stupid_callbacks> */ 

static ebb_element* new_element
  ( void *data
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  if(connection->new_element) 
    return connection->new_element(connection);
  return NULL;
}

static void body_handler
  ( void *data
  , const char *at
  , size_t length
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = ebb_connection_current_request(connection);
  request->body_handler(request, at, length);
}

static void header_handler
  ( void *data
  , ebb_element *field
  , ebb_element *value
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = ebb_connection_current_request(connection);
  request->header_handler(request, field, value);
}

static void request_method
  ( void *data
  , ebb_element *element
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = ebb_connection_current_request(connection);
  request->request_method(request, element);
}

static void request_uri
  ( void *data
  , ebb_element *element
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = ebb_connection_current_request(connection);
  request->request_uri(request, element);
}

static void query_string
  ( void *data
  , ebb_element *element
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = ebb_connection_current_request(connection);
  request->query_string(request, element);
}

static void request_path
  ( void *data
  , ebb_element *element
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = ebb_connection_current_request(connection);
  request->request_path(request, element);
}

static void fragment
  ( void *data
  , ebb_element *element
  )
{
  ebb_connection *connection = (ebb_connection*) data;
  ebb_request *request = ebb_connection_current_request(connection);
  request->fragment(request, element);
}

/* </stupid_callbacks> */ 
  

/* Internal callback 
 * called by connection->timeout_watcher
 */
static void on_timeout 
  ( struct ev_loop *loop
  , ev_timer *watcher
  , int revents
  )
{
  ebb_connection *connection = (ebb_connection*)(watcher->data);

  /* if on_timeout returns true, we don't time out */
  if(connection->on_timeout && connection->on_timeout(connection)) {
    ev_timer_again(loop, watcher);
    return;
  }

  ebb_connection_close(connection);
}

/* Internal callback 
 * called by connection->wrte_watcher
 */
static void on_writable 
  ( struct ev_loop *loop
  , ev_io *watcher
  , int revents
  )
{
  ;
}


/* Internal callback 
 * called by connection->read_watcher
 */
static void on_readable 
  ( struct ev_loop *loop
  , ev_io *watcher
  , int revents
  )
{
  ebb_connection *connection = (ebb_connection*)(watcher->data);

  ebb_buf *buf = NULL;
  if(connection->new_buf)
    buf = connection->new_buf(connection);
  if(buf == NULL)
    return;
  
  ssize_t read = recv( connection->fd
                     , buf->base
                     , buf->len
                     , 0
                     );

  if(read < 0) goto error;
  /* XXX is this the right action to take for read==0 ? */
  if(read == 0) goto error; 

  ev_timer_again(loop, &connection->timeout_watcher);

  ebb_parser_execute( &connection->parser
                    , buf->base
                    , read
                    );

  /* parse error? just drop the client. screw the 400 response */
  if(ebb_parser_has_error(&connection->parser)) goto error;

  return;
error:
  ebb_connection_close(connection);
}


/* Internal callback 
 * Called by server->connection_watcher.
 */
static void on_connection
  ( struct ev_loop *loop
  , ev_io *watcher
  , int revents
  )
{
  ebb_server *server = (ebb_server*)(watcher->data);

  assert(server->listening);
  assert(server->loop == loop);
  assert(&server->connection_watcher == watcher);
  
  if(EV_ERROR & revents) {
    error(0, 0, "on_connection() got error event, closing server.\n");
    ebb_server_unlisten(server);
    return;
  }
  
  struct sockaddr_in addr; // connector's address information
  socklen_t addr_len = sizeof(addr); 
  int fd = accept( server->fd
                 , (struct sockaddr*) & addr
                 , & addr_len
                 );
  if(fd < 0) {
    perror("accept()");
    return;
  }

  ebb_connection *connection = NULL;
  if(server->new_connection)
     connection = server->new_connection(server, &addr);
  if(connection == NULL) {
    close(fd);
    return;
  } 
  
  set_nonblock(fd);
  connection->fd = fd;
  connection->open = TRUE;
  connection->server = server;
  memcpy(&connection->sockaddr, &addr, addr_len);
  
  if(server->port[0] != '\0')
    connection->ip = inet_ntoa(connection->sockaddr.sin_addr);  

  /* Note: not starting the write watcher until there is data to be written */
  ev_io_set(&connection->write_watcher, connection->fd, EV_WRITE);
  ev_io_set(&connection->read_watcher, connection->fd, EV_READ | EV_ERROR);
  /* XXX: seperate error watcher? */

  ev_io_start(loop, &connection->read_watcher);
  ev_timer_start(loop, &connection->timeout_watcher);
}

/**
 * begin the server listening on a file descriptor
 * Thie DOES NOT start the event loop. That is your job.
 * Start the event loop after the server is listening.
 */
int ebb_server_listen_on_fd
  ( ebb_server *server
  , const int sfd 
  )
{
  assert(server->listening == FALSE);

  if (listen(sfd, EBB_MAX_CLIENTS) < 0) {
    perror("listen()");
    return -1;
  }
  
  set_nonblock(sfd); /* XXX superfluous? */
  
  server->fd = sfd;
  server->listening = TRUE;
  
  ev_io_set (&server->connection_watcher, server->fd, EV_READ | EV_ERROR);
  ev_io_start (server->loop, &server->connection_watcher);
  
  return server->fd;
}


/**
 * begin the server listening on a localhost TCP port
 * Thie DOES NOT start the event loop. That is your job.
 * Start the event loop after the server is listening.
 */
int ebb_server_listen_on_port
  ( ebb_server *server
  , const int port
  )
{
  int sfd = -1;
  struct linger ling = {0, 0};
  struct sockaddr_in addr;
  int flags = 1;
  
  if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket()");
    goto error;
  }
  
  flags = 1;
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
  setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
  setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));

  /* TODO: Sending single byte messages in a response?
   * Perhaps need to enable the Nagel algorithm dynamically
   * For now disabling.
   */
  setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
  
  /*
   * the memset call clears nonstandard fields in some impementations
   * that otherwise mess things up.
   */
  memset(&addr, 0, sizeof(addr));
  
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  
  if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind()");
    goto error;
  }
  
  int ret = ebb_server_listen_on_fd(server, sfd);
  if (ret >= 0) {
    sprintf(server->port, "%d", port);
  }
  return ret;
error:
  if(sfd > 0) close(sfd);
  return -1;
}

/**
 * Stops a server from listening. Will not accept new connections.
 * TODO: Drops all connections?
 */
void ebb_server_unlisten
  ( ebb_server *server
  )
{
  if(server->listening) {
    ev_io_stop(server->loop, &server->connection_watcher);
    close(server->fd);
    server->port[0] = '\0';
    server->listening = FALSE;
  }
}

/**
 * Initialize an ebb_server structure.
 * After calling ebb_server_init set the callback server->new_connection 
 * and, optionally, callback data server->data 
 *
 * @params server the server to initialize
 * @params loop a libev loop
 */
void ebb_server_init
  ( ebb_server *server
  , struct ev_loop *loop
  )
{
  server->loop = loop;
  server->listening = FALSE;
  server->port[0] = '\0';
  server->fd = -1;
  server->connection_watcher.data = server;
  ev_init (&server->connection_watcher, on_connection);

  server->new_connection = NULL;
  server->data = NULL;
}

/**
 * Initialize an ebb_connection structure.
 * After calling ebb_connection_init set the callback 
 * connection->new_request 
 * and, optionally, callback data connection->data 
 * 
 * This should be called immediately after allocating space for
 * a new ebb_connection structure. Most likely, this will only 
 * be called within the ebb_server->new_connection callback which
 * you supply. 
 *
 * @params connection the connection to initialize
 * @params timeout    the timeout in seconds
 */
void ebb_connection_init
  ( ebb_connection *connection
  , float timeout
  )
{
  connection->fd = -1;
  connection->server = NULL;
  connection->ip = NULL;
  connection->open = FALSE;
  connection->timeout = timeout;
  
  connection->write_watcher.data = connection;
  ev_init (&connection->write_watcher, on_writable);

  connection->read_watcher.data = connection;
  ev_init(&connection->read_watcher, on_readable);

  connection->timeout_watcher.data = connection;  
  ev_timer_init(&connection->timeout_watcher, on_timeout, timeout, 0);

  /* initialize ebb_parser */
  ebb_parser_init(&connection->parser);
  connection->parser.data = connection;
  connection->parser.new_element = new_element;
  connection->parser.new_request_info = new_request_info;
  connection->parser.request_complete = request_complete;
  connection->parser.body_handler = body_handler;
  connection->parser.header_handler = header_handler;
  connection->parser.request_method = request_method;
  connection->parser.request_uri = request_uri;
  connection->parser.fragment = fragment;
  connection->parser.request_path = request_path;
  connection->parser.query_string = query_string;

  
  connection->new_buf = NULL;
  connection->new_request = NULL;
  connection->on_timeout = NULL;
  connection->free = NULL;
  connection->data = NULL;
}

void ebb_connection_close
  ( ebb_connection *connection
  )
{
  ;
}

ebb_request* ebb_connection_current_request
  ( ebb_connection *connection
  )
{
  return NULL;
}

void ebb_request_init
  ( ebb_request *request
  )
{
  ebb_request_info_init(&request->info);
  request->next = NULL;
  request->connection = NULL;

  request->on_expect_continue = NULL;
  request->on_complete = NULL;
  request->ready_for_write = NULL;

  request->body_handler = NULL;
  request->header_handler = NULL;
  request->request_method = NULL;
  request->request_uri = NULL;
  request->fragment = NULL;
  request->request_path = NULL;
  request->query_string = NULL;

  request->data = NULL;
}
