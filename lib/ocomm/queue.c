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
/**
 * This file implements a FIFO queue as a circular buffer of fixed size.
 */

#include "ocomm/queue.h"
#include "ocomm/o_log.h"

#include <string.h>
#include <stdlib.h>

#define OQUEUE_DONT_CARE_T 0x00
#define OQUEUE_PTR_T 0x01
#define OQUEUE_INT_T 0x02
#define OQUEUE_LONG_T 0x03
#define OQUEUE_FLOAT_T 0x04
#define OQUEUE_DOUBLE_T 0x05
#define OQUEUE_STRING_T 0x06


/* Queue data-structure, to store object state */
typedef struct _queue {

  char* name;       /* Name, used for debugging */

  int    size;      /* Number of items in queue */
  int    max_size;  /* Max number of items allowed in queue */

  OQueueMode mode;  /* Mode to deal with full queue behavior */

  int    step;      /* Max space per queue item to reserve */

  int    qlength;
  char*  queue;
  char*  head;
  char*  tail;

  char nameBuf[64]; /* Local memory for debug name */

} OQueueInt;


/** Create a new OQueue.
 *
 * \param name name of the queue (used for debugging)
 * \param max_size max number of items allowed in the queue
 * \param step max space per item to reserve (e.g., the maximum length of a storable string)
 * \return the newly created OQueue
 */
OQueue*
oqueue_new(
  char* name,
  int max_size,
  int step
) {
  OQueueInt* self = (OQueueInt *)malloc(sizeof(OQueueInt));
  memset(self, 0, sizeof(OQueueInt));

  self->mode = BLOCK_ON_FULL;
  self->step = step;
  self->qlength = max_size * self->step;
  self->queue = (void *)malloc(self->qlength);
  memset(self->queue, 0, self->qlength);
  self->max_size = max_size;
  self->head = self->tail = self->queue;

  self->name = self->nameBuf;
  strcpy(self->name, name != NULL ? name : "UNKNOWN");

  return (OQueue*)self;
}

/** Delete an OQueue.
 *
 * \param queue OQueue to delete
 */
void
oqueue_delete(
  OQueue* queue
) {
  OQueueInt* self = (OQueueInt*)queue;
  free(self->queue);
  self->name = NULL;
  free(self);
}

/** Clear an OQueue.
 *
 * \param queue OQueue to clear
 */
void
oqueue_clear(
  OQueue* queue
) {
  OQueueInt* self = (OQueueInt*)queue;
  self->head = self->tail = self->queue;
  self->size = 0;
}

/* Declared further down, but used by add_data here */
static void* remove_data(OQueue* queue, char type);

/** Enqueue an object into an OQueue.
 *
 * Note, this just stores a pointer to the object.
 *
 * \param queue OQueue where the string should be added
 * \param data pointer to the data buffer to enqueue
 * \param len size of the data to enqueue
 * \param type type of the data to enqueue
 * \return 1 on success, 0 otherwise
 */
static int
add_data(
  OQueue* queue,
  void*   data,
  int     len,
  char    type
) {
  OQueueInt* self = (OQueueInt*)queue;

  if (self->size >= self->max_size) {
    switch (self->mode) {
    case BLOCK_ON_FULL: return 0;
    case DROP_TAIL: return 1;
    case DROP_HEAD: {
      remove_data(queue, OQUEUE_DONT_CARE_T);
      // now there should be space
      break;
    }
    default: {
      o_log(O_LOG_ERROR, "%s: Missing implementation for queue mode %d\n",
          queue->name, self->mode);
      return 0;
    }
    }
  }

  o_log(O_LOG_DEBUG4, "%s: Adding %p (len %d) of type %d at %p\n",
      queue->name, data, len, type, self->tail+1);
  *self->tail = type;
  memcpy(self->tail + 1, data, len);
  self->tail += self->step;
  if (self->tail >= self->queue + self->qlength) {
    self->tail = self->queue;
  }
  self->size++;
  return 1;
}

/** Add a pointer to the OQueue.
 *
 * \param queue OQueue where the string should be added
 * \param obj pointer to add to the queue
 * \return 1 on success, 0 otherwise
 */
int
oqueue_add_ptr(
  OQueue* queue,
  void*   obj
) {
  return add_data(queue, &obj, sizeof(void*), OQUEUE_PTR_T);
}

/** Add an integer to the OQueue.
 *
 * \param queue OQueue where the string should be added
 * \param value integer to add to the queue
 * \return 1 on success, 0 otherwise
 */
int
oqueue_add_int(
  OQueue* queue,
  int     value
) {
  return add_data(queue, &value, sizeof(int), OQUEUE_INT_T);
}

/** Add a long to the queue.
 *
 * \param queue OQueue where the string should be added
 * \param value long to add to the queue
 * \return 1 on success, 0 otherwise
 */
int
oqueue_add_long(
  OQueue* queue,
  long    value
) {
  return add_data(queue, &value, sizeof(long), OQUEUE_LONG_T);
}

/** Add a float to the OQueue.
 *
 * \param queue OQueue where the string should be added
 * \param value float to add to the queue
 * \return 1 on success, 0 otherwise
 */
int
oqueue_add_float(
  OQueue* queue,
  float   value
) {
  return add_data(queue, &value, sizeof(float), OQUEUE_FLOAT_T);
}

/** Add a double to the OQueue.
 *
 * \param queue OQueue where the string should be added
 * \param value double to add to the queue
 * \return 1 on success, 0 otherwise
 */
int
oqueue_add_double(
  OQueue* queue,
  double  value
) {
  return add_data(queue, &value, sizeof(double), OQUEUE_DOUBLE_T);
}


/** Add a string to the OQueue.
 *
 * Note, this just stores a pointer to the string.
 *
 * \param queue OQueue where the string should be added
 * \param string pointer to the first character of the C-string (normal char*)
 * \return 1 on success, 0 otherwise
 */
int
oqueue_add_string(
  OQueue* queue,
  char*   string
) {
  return add_data(queue, string, strlen(string), OQUEUE_STRING_T);
}

/** Remove the oldest object from OQueue and return a pointer to it.
 *
 * Note, that this returns a pointer to the data stored
 * in the OQueue's internal storage. It is the receiver's
 * responsibility to copy it to other storage if the value
 * needs to be maintained (in other words, subsequent adds
 * to the OQueue may override the returned value.
 *
 * \param queue OQueue where the string should be added
 * \param type either OQUEUE_DONT_CARE_T or the expected type of the data to enqueue
 * \return a pointer to the data stored or NULL if queue is empty or the type doesn't match
 */
static void*
remove_data(
  OQueue* queue,
  char    type
) {
  OQueueInt* self = (OQueueInt*)queue;
  void* item = NULL;

  if (self->size <= 0) return NULL;

  if (type != OQUEUE_DONT_CARE_T && *(self->head) != type) {
    o_log(O_LOG_WARN, "%s: Trying to read wrong type from queue\n",
	  self->name);
    return NULL;
  }
  item = self->head + 1;
  self->head += self->step;
  if (self->head >= self->queue + self->qlength) self->head = self->queue;
  self->size--;

  o_log(O_LOG_DEBUG4, "%s: Removed %p of type %d\n",
      queue->name, item, type);

  return item;
}

/** Remove the oldest object as a pointer from OQueue and update a reference to
 * it.
 *
 * \param queue OQueue from which to remove the pointer
 * \param value reference to the variable where the stored pointer should be written
 * \return 1 on success, 0 otherwise
 */
int
oqueue_remove_ptr(
  OQueue* queue,
  void**  value
) {
  void* ptr = remove_data(queue, OQUEUE_PTR_T);

  if (ptr == NULL) return 0;
  *value = *((void**)ptr);
  return 1;
}

/** Remove the oldest object as an integer from OQueue and update a reference to
 * it.
 *
 * \param queue OQueue from which to remove the integer
 * \param value reference to the variable where the stored integer should be written
 * \return 1 on success, 0 otherwise
 */
int
oqueue_remove_int(
  OQueue* queue,
  int*     value
) {
  void* ptr = remove_data(queue, OQUEUE_INT_T);

  if (ptr == NULL) return 0;
  *value = *((int*)ptr);
  return 1;
}

/** Remove the oldest object as a long from OQueue and update a reference to
 * it.
 *
 * \param queue OQueue from which to remove the long
 * \param value reference to the variable where the stored long should be written
 * \return 1 on success, 0 otherwise
 */
int
oqueue_remove_long(
  OQueue* queue,
  long*   value
) {
  void* ptr = remove_data(queue, OQUEUE_LONG_T);

  if (ptr == NULL) return 0;
  *value = *((long*)ptr);
  return 1;
}

/** Remove the oldest object as a float from OQueue and update a reference to
 * it.
 *
 * \param queue OQueue from which to remove the float
 * \param value reference to the variable where the stored float should be written
 * \return 1 on success, 0 otherwise
 */
int
oqueue_remove_float(
  OQueue* queue,
  float*  value
) {
  void* ptr = remove_data(queue, OQUEUE_FLOAT_T);

  if (ptr == NULL) return 0;
  *value = *((float*)ptr);
  return 1;
}

/** Remove the oldest object as a double from OQueue and update a reference to
 * it.
 *
 * \param queue OQueue from which to remove the double
 * \param value reference to the variable where the stored double should be written
 * \return 1 on success, 0 otherwise
 */
int
oqueue_remove_double(
  OQueue* queue,
  double*     value
) {
  void* ptr = remove_data(queue, OQUEUE_DOUBLE_T);

  if (ptr == NULL) return 0;
  *value = *((double*)ptr);
  return 1;
}

/** Remove the oldest object as a string from OQueue and update a reference to
 * it.
 *
 * \param queue OQueue from which to remove the string
 * \param value reference to the variable where the stored string should be written
 * \return 1 on success, 0 otherwise
 */
int
oqueue_remove_string(
  OQueue* queue,
  char**   value
) {
  void* ptr = remove_data(queue, OQUEUE_STRING_T);

  if (ptr == NULL) return 0;
  *value = ptr;
  return 1;
  //  return remove_data(queue, (void**)value, OQUEUE_STRING_T);
}

/** Return the oldest object of the OQueue without removing it.
 *
 * \param queue OQueue in which to peek
 * \return object or NULL if queue is empty
 */
void*
oqueue_peek(
  OQueue* queue
) {
  OQueueInt* self = (OQueueInt*)queue;

  if (self->size <= 0) return NULL;

  return self->tail + 1;
}

/** Check if there is still room in the OQueue.
 *
 * \param queue OQueue to check
 * \return 1 if empty, 0 otherwise
 */
int
oqueue_can_add(
  OQueue* queue
) {
  OQueueInt* self = (OQueueInt*)queue;

  return self->size < self->max_size;
}

/*. Check if OQueue is empty.
 * 
 * \param queue OQueue to check
 * \return 1 if empty, 0 otherwise
 */
int
oqueue_is_empty(
  OQueue* queue
) {
  OQueueInt* self = (OQueueInt*)queue;

  return self->size == 0;
}



/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
