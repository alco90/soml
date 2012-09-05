/*
 * Copyright 2007-2012 National ICT Australia (NICTA), Australia
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
/*!\file client.c
  \brief Simple client connecting to TCP server and send messages
*/

#include <popt.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <assert.h>

#include <ocomm/o_log.h>
#include <ocomm/o_socket.h>
#include <ocomm/o_eventloop.h>

// Network information
#define DEFAULT_PORT 9008
#define ADDR_LENGTH 256 /* RFC 1035, sec. 2.3.4, Size limits: “names  255 octets or less” */
 //************************
static int port = DEFAULT_PORT;
static char addr[ADDR_LENGTH];

#define DEFAULT_LOG_FILE "client.log"
static int log_level = O_LOG_INFO;
static char* logfile_name = DEFAULT_LOG_FILE;


struct poptOption options[] = {
  POPT_AUTOHELP
  { "addr", 'a', POPT_ARG_STRING, &addr, 0,
        "Address to connect to"},
  { "port", 'p', POPT_ARG_INT, &port, 0,
        "Port to receive on"},

  { "debug-level", 'd', POPT_ARG_INT, &log_level, 0,
        "Debug level - error:1 .. debug:4"  },
  { "logfile", 'l', POPT_ARG_STRING, &logfile_name, 0,
        "File to log to", DEFAULT_LOG_FILE },
  { NULL, 0, 0, NULL, 0 }
};


/********************************************/


void
server_callback(
  Socket* source,
  void* handle,
  void* b,
  int buf_size
) {
  char* buf = (char*)b;
  o_log(O_LOG_INFO, "reply: <%s>\n", buf);
  printf("reply: %s\n", buf);
}

static void
shutdown()

{
  socket_close_all();
  exit(0);
}

void
stdin_callback(
  Socket* source,
  void* handle,
  void* b,
  int len
) {
  char* buf = (char*)b;
  char cmd = *buf;
  Socket* outSock = (Socket*)handle;

  o_log(O_LOG_DEBUG, "stdin: <%s>\n", b);

  buf++; len--;
  while (len > 0 && (*buf == ' ' || *buf == '\t')) {
    buf++; len--;
  }
  o_log(O_LOG_DEBUG, "cmd(%c): <%s>\n", cmd, buf);

  switch(cmd) {
  case 'h':
    printf("  m <msg>           .. Send message\n");
    printf("  q                 .. Quit program\n");
    return;

  case 'q':
    shutdown();
    return;

  case 'm':
    o_log(O_LOG_DEBUG, "sending cmd(%d): <%s>\n", len, buf);
    socket_sendto(outSock, buf, len);
    return;

  default:
    o_log(O_LOG_ERROR, "Unknown command '%c'. Type 'h' for list.\n", cmd);
    return;
  }
}

int
main(
  int argc,
  const char *argv[]
) {
  int c;

  *addr = 0;
  strncpy(addr, "localhost", ADDR_LENGTH);

  poptContext optCon = poptGetContext(NULL, argc, argv, options, 0);
  poptSetOtherOptionHelp(optCon, "configFile");
  poptGetNextOpt(optCon);

  while ((c = poptGetNextOpt(optCon)) >= 0);
  o_set_log_file(logfile_name);
  o_set_log_level(log_level);
  setlinebuf(stdout);

  if (c < -1) {
    /* an error occurred during option processing */
    fprintf(stderr, "%s: %s\n",
	    poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
	    poptStrerror(c));
    return -1;
  }

  eventloop_init();

  Socket* sock = socket_tcp_out_new("out", addr, port);
  eventloop_on_read_in_channel(sock, (o_el_read_socket_callback)server_callback, NULL, NULL);

  eventloop_on_stdin((o_el_read_socket_callback)stdin_callback, sock);
  eventloop_run();
  return(0);
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
