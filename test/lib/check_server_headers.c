#include <stdio.h>
#include <check.h>
#include <headers.h>
#include <text.h>
#include <binary.h>
#include "oml_util.h"

/*
 * Used by:  test_tag_from_string
 */
static struct {
  const char *name;
  enum HeaderTag tag;
} vector_header_names [] = {
  { "protocol", H_PROTOCOL },
  { "experiment-id", H_DOMAIN },
  { "sender-id", H_SENDER_ID },
  { "app-name", H_APP_NAME },
  { "content", H_CONTENT },
  { "schema", H_SCHEMA },
  { "start_time", H_START_TIME },
  { "start-time", H_START_TIME },

  { "protocolx", H_NONE },
  { "experiment-idx", H_NONE },
  { "sender-idx", H_NONE },
  { "app-namex", H_NONE },
  { "contentx", H_NONE },
  { "schemax", H_NONE },
  { "start_timex", H_NONE },
  { "start-timex", H_NONE },

  /*
  { "protocol", H_NONE},
  { "experiment-id", H_NONE},
  { "sender-id", H_NONE},
  { "app-name", H_NONE},
  { "content", H_NONE},
  { "schema", H_NONE},
  { "start_time", H_NONE},
  { "start-time", H_NONE},
  */

  { "p", H_NONE},
  { "pr", H_NONE},
  { "pro", H_NONE},
  { "rpotocol", H_NONE},
  { "pretocol", H_NONE},

  { " protocol", H_NONE},
  { "experiment-id ", H_NONE},
  { "sschema", H_NONE },
  { "start time", H_NONE },
  { "starttime", H_NONE },

  { NULL, H_NONE },
};

/*
 * Used by: test_header_from_string
 */
static struct {
  const char *input;
  struct header header;
  int is_null;
  int is_null_short; // NULL in the short test
} vector_headers [] = {
  { "protocol: 3", { H_PROTOCOL, "3", NULL }, 0, 1 },
  { "experiment-id: abc", { H_DOMAIN, "abc", NULL }, 0, 0 },
  { "content: binary", { H_CONTENT, "binary", NULL }, 0, 0 },
  { "content: text  ", { H_CONTENT, "text  ", NULL }, 0, 0 },
  { "content: t", { H_CONTENT, "t", NULL }, 0, 1 },
  { "app-name   :  generator", { H_APP_NAME, "generator", NULL }, 0, 0 },
  { "schema : 1 label:string", { H_SCHEMA, "1 label:string", NULL }, 0, 0 },
  { "start_time: 123456690", { H_START_TIME, "123456690", NULL }, 0, 0 },
  { "start-time: 123456690", { H_START_TIME, "123456690", NULL }, 0, 0 },
  { "domain: abc", { H_DOMAIN, "abc", NULL }, 0, 0 },
  { "", { H_NONE, NULL, NULL }, 1, 1 },
  { " ", { H_NONE, NULL, NULL }, 1, 1 },
  { NULL, { H_NONE, NULL, NULL }, 1, 1 },
  { "not-a-header", { H_NONE, NULL, NULL }, 1, 1 },
  { "not-a-header : with a value", { H_NONE, NULL, NULL }, 1, 1 },
};


START_TEST (test_tag_from_string)
{
  const char *name = vector_header_names[_i].name;
  size_t len = name ? strlen (name) : 1;
  enum HeaderTag expected = vector_header_names[_i].tag;
  enum HeaderTag actual = tag_from_string (name, len);

  fail_unless (actual == expected);
}
END_TEST

START_TEST (test_header_from_string)
{
  struct header *header;
  const char *input = vector_headers[_i].input;
  const struct header *expected = &vector_headers[_i].header;
  const int is_null = vector_headers[_i].is_null;
  size_t len = input ? strlen (input) : 0;

  header = header_from_string(input, len);

  fail_if (!is_null && header == NULL);
  fail_if (is_null  && header != NULL);

  if (!is_null) {
    fail_unless (header->tag == expected->tag, "Incorrect tag, expected '%d', but got '%d'", expected->tag, header->tag);
    fail_unless (strcmp (header->value, expected->value) == 0, "Incorrect header, expected '%s', but got '%s'", expected->value, header->value);
  }
}
END_TEST

START_TEST (test_header_from_string_short)
{
  struct header *header;
  const char *input = vector_headers[_i].input;
  const struct header *expected = &vector_headers[_i].header;
  const int is_null = vector_headers[_i].is_null_short;
  size_t len = input ? strlen (input) : 0;

  /* Adjust so we don't read the whole string, but only if the string
     is long enough */
  if (len > 2)
    len -= 2;

  header = header_from_string(input, len);

  fail_if (!is_null && header == NULL);
  fail_if (is_null  && header != NULL);

  if (!is_null) {
    size_t check_len = strlen (expected->value);
    if (check_len > 2)
      check_len -= 2;
    fail_unless (header->tag == expected->tag);
    fail_unless (header->value && strlen (header->value) == check_len);
    fail_unless (strncmp (header->value, expected->value, check_len) == 0);
  }
}
END_TEST

START_TEST (test_text_read)
{
  uint8_t buf [] = "0.123456\t1\t42\tabde\t3.1416\t111\nbleftover text for next line";
  char meta [] = "1 mympstrm label:string pi:double fighter:uint32";
  MBuffer *mbuf = mbuf_create();
  struct oml_message msg;
  struct schema *schema = schema_from_meta (meta);
  OmlValue values[3];

  bzero(&msg, sizeof(msg));

  mbuf_write (mbuf, buf, sizeof(buf));

  int result = text_read_msg_start (&msg, mbuf);

  fprintf (stderr, "STRM: %d\n", msg.stream);
  fprintf (stderr, "SEQN: %u\n", msg.seqno);
  fprintf (stderr, "TS  : %f\n", msg.timestamp);
  fprintf (stderr, "LEN : %d\n", msg.length);
  fprintf (stderr, "COUNT: %d\n", msg.count);

  result = text_read_msg_values (&msg, mbuf, schema, values);

  result *= 42;
}
END_TEST

START_TEST (test_bin_read)
{
  /* DATA_P, count=1, stream=3, { LONG_T 42 } */
  uint8_t buf [] = { 0xAA, 0xAA, 0x01, 0x00, 0x00,
                     0x3, 0x1, // count = 1, stream = 3
                     0x01, 0x00, 0x00, 0x00, 0x32, // LONG_T 50
                     0x02, 0x54, 0x00, 0x00, 0x00, 0x05, // DOUBLE_T 42.0
                     0x01, 0x00, 0x10, 0xF4, 0x47, // LONG_T 1111111
                     0x02, 0x54, 0x00, 0x00, 0x00, 0x05, // DOUBLE_T 42.0
                     0x04, 0x03, 'A',  'B',  'C' // STRING_T "ABC"
  };
  char meta [] = "3 mympstrm id:long hitchhiker:double sesame:string";
  MBuffer *mbuf = mbuf_create ();
  struct oml_message msg;
  struct schema *schema = schema_from_meta (meta);
  OmlValue values [3];

  bzero(&msg, sizeof(msg));

  int size = sizeof (buf) - 5;
  uint16_t nv = htons (size);
  memcpy (buf + 3, &nv, 2);

  mbuf_write (mbuf, buf, sizeof (buf));

  int result = bin_read_msg_start (&msg, mbuf);

  fprintf (stderr, "---\n");
  fprintf (stderr, "STRM: %d\n", msg.stream);
  fprintf (stderr, "SEQN: %u\n", msg.seqno);
  fprintf (stderr, "TS  : %f\n", msg.timestamp);
  fprintf (stderr, "LEN : %d\n", msg.length);
  fprintf (stderr, "COUNT: %d\n", msg.count);

  result = bin_read_msg_values (&msg, mbuf, schema, values);

  int i = 0;
  for (i = 0; i < 3; i++) {
    char s[64];
    oml_value_to_s (&values[i].value, values[i].type, s);
    fprintf (stderr, "%s\n", s);
  }
}
END_TEST

Suite *
headers_suite (void)
{
  Suite *s = suite_create ("Headers");

  /* Headers test cases */
  TCase *tc_tag_from_string = tcase_create ("HeadersTagFromString");
  TCase *tc_header_from_string = tcase_create ("HeadersHeaderFromString");

  /* Add tests to test case "HeadersTagFromString" */
  tcase_add_loop_test (tc_tag_from_string, test_tag_from_string,
                       0, LENGTH (vector_header_names));
  tcase_add_loop_test (tc_header_from_string, test_header_from_string,
                       0, LENGTH (vector_headers));
  tcase_add_loop_test (tc_header_from_string, test_header_from_string_short,
                       0, LENGTH (vector_headers));

  tcase_add_test (tc_header_from_string, test_text_read);
  tcase_add_test (tc_header_from_string, test_bin_read);

  suite_add_tcase (s, tc_tag_from_string);
  suite_add_tcase (s, tc_header_from_string);

  return s;
}
