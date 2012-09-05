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
/*!\file queue.c
  \brief testing the queue
*/

#include <string.h>
#include <check.h>

#include "ocomm/queue.h"

START_TEST (test_queue_fill_empty)
{
  OQueue* q = oqueue_new("test_queue_fill_empty", 3, sizeof(char*));
  oqueue_add_string(q, "one");
  oqueue_add_string(q, "two");
  char* s1;  oqueue_remove_string(q, &s1);
  char* s2;  oqueue_remove_string(q, &s2);
  fail_if(strcmp(s1,"one"), "First dequeued element (%s) is not 'one'", s1);
  fail_if(strcmp(s2,"two"), "Second dequeued element (%s) is not 'two'", s2);
  oqueue_delete(q);
}
END_TEST

START_TEST (test_queue_wrap)
{
  OQueue* q = oqueue_new("test_queue_wrap", 3, sizeof(char*));
  oqueue_add_string(q, "one");
  oqueue_add_string(q, "two");
  char* s1;  oqueue_remove_string(q, &s1);
  char* s2;  oqueue_remove_string(q, &s2);
  oqueue_add_string(q, "one");
  oqueue_add_string(q, "two");
  oqueue_remove_string(q, &s1);
  oqueue_remove_string(q, &s2);
  fail_if(strcmp(s1,"one"), "First dequeued element (%s; second try) is not 'one'", s1);
  fail_if(strcmp(s2,"two"), "Second dequeued element (%s; second try) is not 'two'", s2);
  oqueue_delete(q);
}
END_TEST

START_TEST (test_queue_full)
{
  OQueue* q = oqueue_new("test_queue_full", 3, sizeof(char*));
  fail_unless(oqueue_add_string(q, "one"), "oqueue_add_string() failed adding an element to a list");
  fail_unless(oqueue_add_string(q, "two"), "oqueue_add_string() failed adding an element to a list");
  fail_unless(oqueue_add_string(q, "three"), "oqueue_add_string() failed adding an element to a list");
  fail_if(oqueue_add_string(q, "four"), "oqueue_add_string() succeded adding an element to a full list");
  char* s1; oqueue_remove_string(q, &s1);
  fail_if(strcmp(s1,"one"), "First dequeued element (%s) is not 'one'", s1);
  oqueue_delete(q);
}
END_TEST

START_TEST (test_queue_types)
{
  int i = 1111111111, io, *ip;
  long l = 222222222L, lo;
  float f = 333.333, fo;
  double d = 4.444444444444444444444444444, dou;
  OQueue* q = oqueue_new("test_queue_types", 5, sizeof(void*));

  oqueue_add_ptr(q, &i);
  oqueue_add_int(q, i);
  oqueue_add_long(q, l);
  oqueue_add_float(q, f);
  oqueue_add_double(q, d);

  oqueue_remove_ptr(q, &ip);
  fail_unless(ip == &i, "Dequeued pointer element (%p) is not %p", ip, &i);

  oqueue_remove_int(q, &io);
  fail_unless(io == i, "Dequeued int element (%d) is not %d", io, i);

  oqueue_remove_long(q, &lo);
  fail_unless(lo == l, "Dequeued long element (%ld) is not %ld", lo, l);

  oqueue_remove_float(q, &fo);
  fail_unless(fo == f, "Dequeued float element (%f) is not %f", fo, f);

  oqueue_remove_double(q, &dou);
  fail_unless(dou == d, "Dequeued double element (%e) is not %e", dou, d);

  oqueue_delete(q);
}
END_TEST

Suite* 
queue_suite(void)
{
  Suite* s = suite_create("queue");
  TCase* tc_queue = tcase_create ("queue");

  tcase_add_test(tc_queue, test_queue_fill_empty);
  tcase_add_test(tc_queue, test_queue_wrap);
  tcase_add_test(tc_queue, test_queue_full);
  tcase_add_test(tc_queue, test_queue_types);
  
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
