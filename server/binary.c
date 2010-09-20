
#include <oml2/omlc.h>
#include <mbuf.h>
#include <marshal.h>
#include <oml_value.h>
#include "schema.h"
#include "message.h"

#ifndef SYNC_BYTE
#define SYNC_BYTE 0xAA
#endif

#ifndef OMB_DATA_P
#define OMB_DATA_P 0x1
#endif

#ifndef OMB_LDATA_P
#define OMB_LDATA_P 0x2
#endif

static int
bin_read_value (MBuffer *mbuf, OmlValue *value)
{
  return unmarshal_value (mbuf, value);
}

static int
bin_find_sync (MBuffer *mbuf)
{
  int pos;
  do {
    if (mbuf_remaining (mbuf) < 2)
      return -1;

    uint8_t *buf = mbuf_rdptr (mbuf);
    if (buf[0] == SYNC_BYTE && buf[1] == SYNC_BYTE)
      return 0;
    mbuf_read_skip (mbuf, 1);
    pos = mbuf_find (mbuf, SYNC_BYTE);
    mbuf_read_skip (mbuf, pos);
  } while (pos != -1);

  return -1;
}

/*
 * Read the start of the new message; detect which stream it belongs
 * to, what the length of the message is, the sequence number, and the
 * timestamp.  Fill in the msg struct with this information.
 */
int
bin_read_msg_start (struct oml_message *msg, MBuffer *mbuf)
{
  if (msg == NULL || mbuf == NULL)
    return -1;

  /* First, find the sync position */
  int sync_pos = bin_find_sync (mbuf);

  if (sync_pos == -1)
    return -1;

  mbuf_begin_read (mbuf);

  if (mbuf_remaining (mbuf) < 3) {
    return -1; // Not enough data to determine packet type
  }

  mbuf_read_skip (mbuf, 2); // Skip the sync bytes

  uint8_t packet_type;
  uint16_t msglen16;
  uint32_t length;
  uint32_t header_length;
  mbuf_read (mbuf, &packet_type, 1);

  switch (packet_type) {
  case OMB_DATA_P:
    // FIXME:  Return type (maybe not enough bytes)
    mbuf_read (mbuf, &msglen16, 2);
    msglen16 = ntohs (msglen16);
    length = (uint32_t)msglen16;
    header_length = 5;
    break;
  case OMB_LDATA_P:
    mbuf_read (mbuf, &length, 4);
    length = ntohl (length);
    header_length = 7;
    break;
  default:
    return -1; // Unknown packet type
  }

  if (mbuf_remaining (mbuf) < length)
    return 0; /* Not enough bytes for the full message */

  // Now get the count and index
  uint8_t count;
  uint8_t stream;

  mbuf_read (mbuf, &count, 1);
  mbuf_read (mbuf, &stream, 1);

  msg->type = MSG_BINARY;
  msg->stream = stream;
  msg->length = length;
  msg->count = count;

  OmlValue value;

  value.type = OML_LONG_VALUE;
  // FIXME: check for error (e.g. type mismatch)
  bin_read_value (mbuf, &value);
  msg->seqno = value.value.longValue;

  value.type = OML_DOUBLE_VALUE;
  // FIXME: check for error (e.g. type mismatch)
  bin_read_value (mbuf, &value);
  msg->timestamp = value.value.doubleValue;

  return msg->length;
}

int
bin_read_msg_values (struct oml_message *msg, MBuffer *mbuf, struct schema *schema, OmlValue *values)
{
  int index = mbuf_message_index (mbuf);
  int i = 0;

  if (msg->count != schema->nfields)
    return -1;



  for (i = 0; i < schema->nfields; i++) {
    int bytes;

    bytes = bin_read_value (mbuf, &values[i]);

    if (bytes == -1)
      return -1;
  }

  mbuf_consume_message (mbuf);
  return 0;
}