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
/*!\file mt_queue.c
  \brief testing the thread-safe queue
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <check.h>

#include "ocomm/o_log.h"
#include "ocomm/mt_queue.h"

char* data[] = {
  "token1",
  "token2",
  "token3",
  "token4",
  "token5",
};

void*
fast_producer(
  void* queue
) {
  int i;
  MTQueue *q = (MTQueue*)queue;

  for (i = 0; i < 5; i++) {
    o_log(O_LOG_DEBUG, "fp(%d): Adding '%s'\n", i, data[i]);
    fail_unless(mt_queue_add(q, data[i]), "Cannot add data '%s' into queue", data[i]);
  }
  return NULL;
}

void*
slow_producer(
  void* queue
) {
  int i;
  MTQueue *q = (MTQueue*)queue;

  for (i = 0; i < 5; i++) {
    o_log(O_LOG_DEBUG, "sp(%d): Adding '%s'\n", i, data[i]);
    fail_unless(mt_queue_add(q, data[i]), "Cannot add data '%s' into queue", data[i]);
    sleep(1);
  }
  return NULL;
}

void
fast_consumer(
  MTQueue* q,
  int samples
) {
  int i;

  for (i = 0; i < samples; i++) {
    char *res;
    res = (char *)mt_queue_remove(q);
    fail_unless(res, "Got NULL data");
    o_log(O_LOG_DEBUG, "fc: Removed '%s' (%s)\n", res, data[i]);
    fail_if(strcmp(res, data[i]), "Dequeued element '%s' is not '%s'", res, data[i]);
  }
}

void
slow_consumer(
  MTQueue* q,
  int samples
) {
  int i;

  for (i = 0; i < samples; i++) {
    char *res;
    res = (char *)mt_queue_remove(q);
    fail_unless(res, "Got NULL data");
    o_log(O_LOG_DEBUG, "sc: Removed '%s' (%s)\n", res, data[i]);
    fail_if(strcmp(res, data[i]), "Dequeued element '%s' is not '%s'", res, data[i]);
    sleep(1);
  }
}

START_TEST (test_mt_queue_fast_consumer)
{
  MTQueue* q;
  pthread_t  thread;

  q = mt_queue_new("Qfc", 3);
  pthread_create(&thread, NULL, slow_producer, (void*)q);
  fast_consumer(q, 5);
  o_log(O_LOG_DEBUG2, "tfc: Joining thread\n");
  pthread_join(thread, NULL);
  o_log(O_LOG_DEBUG2, "tfc: Joined\n");
  mt_queue_delete(q);
}
END_TEST

START_TEST (test_mt_queue_slow_consumer)
{
  MTQueue* q;
  pthread_t  thread;

  q = mt_queue_new("Qsc", 3);
  pthread_create(&thread, NULL, fast_producer, (void*)q);
  slow_consumer(q, 5);
  o_log(O_LOG_DEBUG2, "tsc: Joining thread\n");
  pthread_join(thread, NULL);
  o_log(O_LOG_DEBUG2, "tfc: Joined\n");
  mt_queue_delete(q);
}
END_TEST

Suite*
mt_queue_suite(void)
{
  Suite* s = suite_create("mt_queue");
  TCase* tc_queue = tcase_create ("mt_queue");

  tcase_set_timeout(tc_queue, 10);

  tcase_add_test(tc_queue, test_mt_queue_fast_consumer);
  tcase_add_test(tc_queue, test_mt_queue_slow_consumer);

  suite_add_tcase(s, tc_queue);

  return s;
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
