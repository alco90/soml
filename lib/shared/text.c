
#include <mbuf.h>
#include <marshal.h>
#include <oml_value.h>
#include "schema.h"
#include "message.h"

/**
 *  @brief Read an OmlValue value from +mbuf+.
 *
 *  The mbuf read pointer is assumed to be pointing to the start of a
 *  value.  The current line must be completely contained in the mbuf,
 *  including the final newline '\n'.  A value is read from the mbuf
 *  assuming that its type matches the one contained in the value
 *  object.
 *
 *  Values are assumed to be tab-delimited, and so either a tab or a
 *  newline terminates the next field in the buffer.  This function
 *  modifies the mbuf contents by null-terminating the field,
 *  replacing the tab or newline with '\0'.
 *
 *  After this function finishes, if it was successful, the mbuf read
 *  pointer (mbuf_rdptr()) will be modified to point to the first
 *  character following the field.
 *
 *  @mbuf The buffer containing the next field value
 *  @value An OmlValue to store the parse value in. The type of this
 *  value on input determines what type of value the function should
 *  attempt to read.
 *  @line_length length of the current line in the buffer, measured
 *  from the buffer's current read pointer (mbuf_rdptr()).
 *
 *  @return -1 if an error occurred; otherwise, the length of the
 *  field in bytes.
 *
 */
static int
text_read_value (MBuffer *mbuf, OmlValue *value, size_t line_length)
{
  uint8_t *line = mbuf_rdptr (mbuf);
  int field_length = mbuf_find (mbuf, '\t');
  int len;
  int ret = 0;
  uint8_t save;

  /* No tab '\t' found on this line --> final field */
  if (field_length == -1 || field_length > line_length)
    len = line_length;
  else
    len = field_length;

  save = line[len];
  line[len] = '\0';

  ret = oml_value_from_s (value, line);
  line[len] = save;

  len++; // Skip the separator
  if (ret == -1) {
    return ret;
  } else {
    mbuf_read_skip (mbuf, len);
    return len;
  }
}

int
text_read_msg_start (struct oml_message *msg, MBuffer *mbuf)
{
  uint8_t *line = mbuf_rdptr (mbuf);
  uint8_t *end;
  int len = mbuf_find (mbuf, '\n'); // Find length of line
  OmlValue value;
  int bytes = 0;

  if (len == -1)
    return 0; // Haven't got a full line

  msg->length = (uint32_t)len;

  /* Read the timestamp first */
  value.type = OML_DOUBLE_VALUE;
  bytes = text_read_value (mbuf, &value, len);
  if (bytes == -1)
    return -1;
  else
    len -= bytes;

  msg->timestamp = value.value.doubleValue;

  /* Read the stream index */
  value.type = OML_UINT32_VALUE;
  bytes = text_read_value (mbuf, &value, len);
  if (bytes == -1)
    return -1;
  else
    len -= bytes;

  msg->stream = value.value.uint32Value;

  /* Read the sequence number */
  value.type = OML_UINT32_VALUE;
  bytes = text_read_value (mbuf, &value, len);
  if (bytes == -1)
    return -1;
  else
    len -= bytes;

  msg->seqno = (uint32_t)value.value.uint32Value;
  return msg->length;
}

/**
 *  @brief Read a vector of values matching schema from mbuf.
 *
 *  Reads as many values as required to match the schema from the
 *  mbuf, and stores them in the values vector.
 *
 */
int
text_read_msg_values (struct oml_message *msg, MBuffer *mbuf, struct schema *schema, OmlValue *values)
{
  int length = msg->length;
  int index = mbuf_message_index (mbuf);
  int msg_remaining = length - index;
  int i = 0;

  length = msg_remaining;

  for (i = 0; i < schema->nfields; i++) {
    int bytes;
    values[i].type = schema->fields[i].type;
    bytes = text_read_value (mbuf, &values[i], length);

    if (bytes == -1)
      return -1;
    else
      length -= bytes;
  }

  msg->count = schema->nfields;
  // rdptr now points to start of next line/message
  mbuf_consume_message (mbuf);
  return 0;
}
