
#include <mem.h>
#include <mbuf.h>
#include <headers.h>
#include <message.h>
#include <text.h>
#include <binary.h>
#include "proxy_client_handler.h"


/**
 * @brief Read a line from mbuf.
 *
 * Sets *line_p to point to the start of the line, and *length_p to
 * the length of the line and returns 0 if successful.  Note that
 * *line_p points into the mbuf internal buffer, and the line is not
 * '\0'-terminated, i.e. the data isn't touched.
 *
 * If there is no newline character, returns -1 and *line_p and
 * *length_p are untouched.
 *
 * @param line_p out parameter for the start of the line
 * @param length_p out parameter for the length of the line
 * @param mbuf the buffer that contains the data to be read
 * @return 1 if successful, 0 otherwise
 */
static int
read_line(char** line_p, int* length_p, MBuffer* mbuf)
{
  uint8_t* line = mbuf_rdptr (mbuf);
  int length = mbuf_find (mbuf, '\n');

  // No newline found
  if (length == -1)
    return -1;

  *line_p = (char*)line;
  *length_p = length;
  return 0;
}



int
read_header (Client *self, MBuffer *mbuf)
{
  char *line;
  int len;
  struct header *header;

  if (self == NULL || mbuf == NULL) {
    logerror ("read_header():  NULL client or input buffer.\n");
    return 0;
  }

  if (read_line (&line, &len, mbuf) == -1)
    return 0;

  if (len == 0) {
    // Empty line denotes separator between header and body
    int skip_count = mbuf_find_not (mbuf, '\n');
    mbuf_read_skip (mbuf, skip_count);
    self->state = C_CONFIGURE;
    return 0;
  }

  header = header_from_string (line, len);
  mbuf_read_skip (mbuf, len + 1);
  if (header == NULL) {
    /* Could be protocol error (not ':'), memory alloc error, or unknown tag */
    self->state = C_PROTOCOL_ERROR;
    return -1;
  }

  /* Store the header for later processing */
  header->next = self->headers;
  self->headers = header;
  if (header->tag != H_NONE) {
    self->header_table[header->tag] = header;
  }

  return 1; // still in header
}

/**
 * @brief Convert a content header into the correct symbol type.
 *
 * @param str the string represention of the content type, either
 * "text" or "binary".
 * @return The symbolic representation of the content type, either
 * CONTENT_BINARY, CONTENT_TEXT, or CONTENT_NONE to indicate an unrecognized
 * content type string.
 */
enum ContentType
content_from_string (const char *str)
{
  if (str == NULL)
    return CONTENT_NONE;

  if (strcmp (str, "binary") == 0)
    return CONTENT_BINARY;
  else if (strcmp (str, "text") == 0)
    return CONTENT_TEXT;

  return CONTENT_NONE;
}



void
proxy_message_loop (const char *client_id, Client *client, void *buf, size_t size)
{
  MBuffer *mbuf = client->mbuf;
  struct header *header;
  struct oml_message msg;
  int result;

  result = mbuf_write (mbuf, buf, size);
  if (result == -1) {
    logerror ("'%s': Failed to write message from client into message buffer. Data is being lost!\n",
              client_id);
    return;
  }

 loop:
  switch (client->state) {
  case C_HEADER:
    /* Read headers until out of header data to read*/
    while (read_header (client, mbuf) == 1);
    if (client->state != C_HEADER)
      goto loop;
    break;
  case C_CONFIGURE:
    if (client->header_table[H_EXPERIMENT_ID] == NULL ||
        client->header_table[H_CONTENT] == NULL ||
        client->header_table[H_EXPERIMENT_ID]->tag == H_NONE ||
        client->header_table[H_CONTENT]->tag == H_NONE) {
      if (client->headers == NULL) {
        logdebug ("Headers NULL!");
      }
      client->state = C_PROTOCOL_ERROR;
    } else {
      client->experiment_id = client->header_table[H_EXPERIMENT_ID]->value;
      client->content = content_from_string (client->header_table[H_CONTENT]->value);
      if (client->content == CONTENT_NONE)
        client->state = C_PROTOCOL_ERROR;
    }
    if (client->state != C_PROTOCOL_ERROR) {
      logdebug ("%s\n", client->experiment_id);
      logdebug ("%d\n", client->content);
    } else {
      logdebug ("Can't write out experiment id and content because of protocol error in input\n");
      logdebug ("Input is: '%s'\n", buf);
    }
    for (header = client->headers; header != NULL; header = header->next) {
      logdebug ("HEADER:  '%s' : '%s'\n",
                tag_to_string(header->tag),
                header->value);
    }
    switch (client->content){
    case CONTENT_TEXT:
      client->msg_start = text_read_msg_start;
      break;
    case CONTENT_BINARY:
      client->msg_start = bin_read_msg_start;
      break;
    default:
      /* Default is set when client is created (client_new()) */
      break;
    }
    mbuf_consume_message (mbuf); // Next message starts after the headers.
    client->state = C_DATA;
    break;
  case C_DATA:
    result = client->msg_start (&msg, mbuf);
    if (result == -1) {
      logerror ("'%s': protocol error in received message\n", client_id);
      client->state = C_PROTOCOL_ERROR;
      goto loop;
    } else if (result == 0) {
      // Try again when we get more data.
      logdebug ("'%s': need more data\n", client_id);
      mbuf_reset_read (mbuf);
      return;
    }
    logdebug ("Recieved [strm=%d seqno=%d ts=%f %d bytes]\n",
              msg.stream, msg.seqno, msg.timestamp, msg.length);
    mbuf_reset_read (mbuf);
    char *s = xstrndup ((char*)mbuf_rdptr (mbuf), msg.length);
    if (client->content == CONTENT_BINARY) {
      logdebug ("%s\n", to_octets (s, msg.length));
      printf ("%s\n", to_octets (s, msg.length));
    } else {
      logdebug ("'%s'\n", s);
      printf ("'%s'\n", s);
    }

    mbuf_read_skip (mbuf, msg.length + 1);
    mbuf_consume_message (mbuf);
    break;
  case C_PROTOCOL_ERROR:
    logdebug ("'%s': protocol error!\n");
    fprintf (stderr, "Protocol error!\n");
    break;
  default:
    logerror ("'%s': unknown client state '%d'\n", client_id, client->state);
    fprintf (stderr, "Unknown client state '%d'\n");
    mbuf_clear (mbuf);
    return;
  }
}
