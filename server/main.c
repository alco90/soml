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

/** Main oml2-server functions */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <popt.h>
#include <signal.h>
#include <errno.h>

#include <oml2/oml_writer.h>
#include <log.h>
#include <mem.h>
#include <ocomm/o_socket.h>
#include <ocomm/o_eventloop.h>

#include "oml_util.h"
#include "version.h"
#include "hook.h"
#include "client_handler.h"
#include "sqlite_adapter.h"
#if HAVE_LIBPQ
#include <libpq-fe.h>
#include "psql_adapter.h"
#endif

void
die (const char *fmt, ...)
{
  va_list va;
  va_start (va, fmt);
  o_vlog (O_LOG_ERROR, fmt, va);
  va_end (va);
  exit (EXIT_FAILURE);
}

#define DEFAULT_PORT 3003
#define DEFAULT_PORT_STR "3003"
#define DEFAULT_LOG_FILE "oml_server.log"
#define DEFAULT_PG_CONNINFO "host=localhost"
#define DEFAULT_PG_USER "oml"
#define DEFAULT_DB_BACKEND "sqlite"

static int listen_port = DEFAULT_PORT;
extern char *sqlite_database_dir = NULL;
#if HAVE_LIBPQ
extern char *pg_conninfo = DEFAULT_PG_CONNINFO;
extern char *pg_user = DEFAULT_PG_USER;
#endif

static int log_level = O_LOG_INFO;
static char* logfile_name = NULL;
static char* backend = DEFAULT_DB_BACKEND;
static char* uidstr = NULL;
static char* gidstr = NULL;

struct poptOption options[] = {
  POPT_AUTOHELP
  { "listen", 'l', POPT_ARG_INT, &listen_port, 0, "Port to listen for TCP based clients", DEFAULT_PORT_STR},
  { "backend", 'b', POPT_ARG_STRING, &backend, 0, "Database server backend", DEFAULT_DB_BACKEND},
  { "data-dir", 'D', POPT_ARG_STRING, &sqlite_database_dir, 0, "Directory to store database files (sqlite)", "DIR" },
#if HAVE_LIBPQ
  { "pg-user", '\0', POPT_ARG_STRING, &pg_user, 0, "PostgreSQL user to connect as", DEFAULT_PG_USER },
  { "pg-connect", '\0', POPT_ARG_STRING, &pg_conninfo, 0, "PostgreSQL connection info string", "\"" DEFAULT_PG_CONNINFO "\""},
#endif
  { "user", '\0', POPT_ARG_STRING, &uidstr, 0, "Change server's user id", "UID" },
  { "group", '\0', POPT_ARG_STRING, &gidstr, 0, "Change server's group id", "GID" },
  { "event-hook", 'H', POPT_ARG_STRING, &hook, 0, "Path to an event hook taking input on stdin", "HOOK" },
  { "debug-level", 'd', POPT_ARG_INT, &log_level, 0, "Increase debug level", "{1 .. 4}"  },
  { "logfile", '\0', POPT_ARG_STRING, &logfile_name, 0, "File to log to", DEFAULT_LOG_FILE },
  { "version", 'v', 0, 0, 'v', "Print version information and exit", NULL },
  { NULL, 0, 0, NULL, 0, NULL, NULL }
};

static struct db_backend
{
  const char * const name;
  db_adapter_create fn;
} backends [] =
  {
    { "sqlite", sq3_create_database },
#if HAVE_LIBPQ
    { "postgresql", psql_create_database },
#endif
  };

const char *
valid_backends ()
{
  static char s[256] = {0};
  int i;
  if (s[0] == '\0') {
    char *p = s;
    for (i = LENGTH (backends) - 1; i >= 0; i--) {
      int n = sprintf(p, "%s", backends[i].name);
      if (i) {
        p[n++] = ',';
        p[n++] = ' ';
      }
      p[n] = '\0';
      p += n;
    }
  }
  return s;
}

db_adapter_create
database_create_function (char *selected_backend)
{
  size_t i = 0;
  for (i = 0; i < LENGTH (backends); i++)
    if (!strncmp (selected_backend, backends[i].name, strlen (backends[i].name)))
      return backends[i].fn;

  return NULL;
}

/**
 * @brief Set up the logging system.
 *
 * This function sets up the server logging system to log to file
 * logfile, with the given log verbosity level.  All messages with
 * severity less than or equal to level will be logged; all others
 * will be discarded (lower levels are more important).
 *
 * If logfile is not NULL then the named file is opened for logging.
 * If logfile is NULL and the application's stderr stream is not
 * attached to a tty, then the file DEF_LOG_FILE is opened for
 * logging; otherwise, if logfile is NULL and stderr is attached to a
 * tty then log messages will sent to stderr.
 *
 * @param logfile the file to open
 * @param level the severity level at which to log
 */
void
setup_logging (char *logfile, int level)
{
  if (!logfile) {
    if (isatty (fileno (stderr)))
      logfile = "-";
    else
      logfile = DEFAULT_LOG_FILE;
  }

  o_set_log_file(logfile);
  o_set_log_level(level);
  o_set_simplified_logging ();
}

void
setup_backend (void)
{
  logdebug ("Database backend: '%s'\n", backend);

  if (!database_create_function (backend))
    die ("Unknown database backend '%s' (valid backends: %s)\n",
         backend, valid_backends ());

  if (!strcmp (backend, "sqlite"))
    if(sq3_backend_setup ())
      exit(EXIT_FAILURE);
#if HAVE_LIBPQ
  else if (!strcmp (backend, "postgresql"))
    if(psql_backend_setup ())
      exit(EXIT_FAILURE);
#endif
}

/** Signal handler
 *
 * Captures the following signals, and handles them thusly.
 * * SIGTERM: instruct the EventLoop to stop.
 *
 * \see eventloop_stop()
 */
static void sighandler(int signum)
{
  switch(signum) {
  case SIGINT:
  case SIGTERM:
    loginfo("Received signal %d, cleaning up and exiting\n", signum);
    eventloop_stop(signum);
    break;
  case SIGUSR1:
    xmemreport();
    break;
  default:
    logwarn("Received unhandled signal %d\n", signum);
  }
}

/** Set up the signal handler.
 *
 * \see sighandler
 */
void setup_signal (void)
{
  struct sigaction sa;

  logdebug("Installing signal handlers\n");

  sa.sa_handler = sighandler;
  sigemptyset (&sa.sa_mask);
  sa.sa_flags = 0;

  if(sigaction(SIGTERM, &sa, NULL))
    logwarn("Unable to install SIGTERM handler: %s\n", strerror(errno));
  if(sigaction(SIGINT, &sa, NULL))
    logwarn("Unable to install SIGINT handler: %s\n", strerror(errno));
  if(sigaction(SIGUSR1, &sa, NULL))
    logwarn("Unable to install SIGUSR1 handler: %s\n", strerror(errno));
}

void
drop_privileges (const char *uidstr, const char *gidstr)
{
  if (gidstr && !uidstr)
    die ("--gid supplied without --uid\n");

  if (uidstr) {
    struct passwd *passwd = getpwnam (uidstr);
    gid_t gid;

    if (!passwd)
      die ("User '%s' not found\n", uidstr);
    if (!gidstr)
      gid = passwd->pw_gid;
    else {
      struct group *group = getgrnam (gidstr);
      if (!group)
        die ("Group '%s' not found\n", gidstr);
      gid = group->gr_gid;
    }

    struct group *group = getgrgid (gid);
    const char *groupname = group ? group->gr_name : "??";
    gid_t grouplist[] = { gid };

    if (setgroups (1, grouplist) == -1)
      die ("Couldn't restrict group list to just group '%s': %s\n", groupname, strerror (errno));

    if (setgid (gid) == -1)
      die ("Could not set group id to '%s': %s", groupname, strerror (errno));

    if (setuid (passwd->pw_uid) == -1)
      die ("Could not set user id to '%s': %s", passwd->pw_name, strerror (errno));

    if (setuid (0) == 0)
      die ("Tried to drop privileges but we seem able to become superuser still!\n");
  }
}

/** Callback called when a new connection is received on the listening Socket.
 *
 * This function creates a ClientHandler to manage the data from this Socket.
 * The listening Socket would have been created using socket_server_new().
 *
 * \param new_sock Socket object created by accept()ing the connection
 * \param handle pointer to opaque data passed when creating the listening Socket
 *
 * \see socket_server_new()
 * \see on_client_connect()
 */
void
on_connect(Socket* new_sock, void* handle)
{
  (void)handle;
  ClientHandler *client = client_handler_new(new_sock);
  logdebug("%s: New client connected\n", client->name);
}

int
main(int argc, const char **argv)
{
  int c;
  poptContext optCon = poptGetContext(NULL, argc, argv, options, 0);

  while ((c = poptGetNextOpt(optCon)) >= 0) {
    switch (c) {
    case 'v':
      printf(V_STRING, VERSION);
      printf("OML Protocol V%d\n", OML_PROTOCOL_VERSION);
      printf(COPYRIGHT);
      return 0;
    }
  }

  setup_logging (logfile_name, log_level);

  if (c < -1)
    die ("%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));

  loginfo(V_STRING, VERSION);
  loginfo("OML Protocol V%d\n", OML_PROTOCOL_VERSION);
  loginfo(COPYRIGHT);

  eventloop_init();

  Socket* server_sock;
  server_sock = socket_server_new("server", listen_port, on_connect, NULL);

  if (!server_sock)
    die ("Failed to create listening socket on port %d\n", listen_port);

  drop_privileges (uidstr, gidstr);

  /* Important that this comes after drop_privileges().  See setup_backend_sqlite() */
  setup_backend ();

  setup_signal();

  hook_setup();

  eventloop_run();

  hook_cleanup();

  xmemreport();

  return 0;
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
