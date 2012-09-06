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
 * This file implements a thread-safe OQueues.
 */

#include "ocomm/mt_queue.h"
#include "ocomm/queue.h"
#include "ocomm/o_log.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>


/* Queue data-structure, to store object state */
typedef struct _mt_queue {

  char* name;       /* Name, used for debugging */

  OQueue* queue;    /* OQueue use for actual storage */

  pthread_mutex_t mutex;
  pthread_cond_t  writeCondVar;
  pthread_cond_t  readCondVar;


  char nameBuf[64]; /* Local memory for debug name */

} MTQueueInt;

static int lock(MTQueueInt* self);
static void unlock(MTQueueInt* self);

/** Create a new MTQueue.
 *
 * \param name name of the queue (used for debugging)
 * \param length max number of items allowed in queue
 * \return the newly created MTQueue
 */
MTQueue*
mt_queue_new(
  char* name,
  int length
) {
  MTQueueInt* self = (MTQueueInt *)malloc(sizeof(MTQueueInt));
  memset(self, 0, sizeof(MTQueueInt));

  self->queue = oqueue_new(name, length, sizeof(void*));

  pthread_mutex_init(&self->mutex, NULL);
  pthread_cond_init(&self->writeCondVar, NULL);
  pthread_cond_init(&self->readCondVar, NULL);

  self->name = self->nameBuf;
  strcpy(self->name, name != NULL ? name : "UNKNOWN");

  return (MTQueue*)self;
}

/** Delete an MTQueue.
 *
 * \param queue MTQueue to delete
 */
void
mt_queue_delete(
  MTQueue* queue
) {
  MTQueueInt* self = (MTQueueInt*)queue;
  free(self->queue);
  pthread_mutex_destroy(&self->mutex);
  pthread_cond_destroy(&self->writeCondVar);
  pthread_cond_destroy(&self->readCondVar);
  self->name = NULL;
  free(self);
}


/** Enqueue an object into an MTQueue.
 *
 * Note, this just stores a pointer to the object.
 *
 * \param queue MTQueue where the string should be added
 * \param obj pointer to the data buffer to enqueue
 * \return 1 on success, 0 otherwise
 */
int
mt_queue_add(
  MTQueue* queue,
  void*  obj
) {
  MTQueueInt* self = (MTQueueInt*)queue;

  if (!lock(self)) return 0;
  int rc = 0;
  while(rc != 0 || !oqueue_add_ptr(self->queue, obj)) {
    rc = pthread_cond_wait(&self->writeCondVar, &self->mutex);
  }
  unlock(self);

  if (pthread_cond_signal(&self->readCondVar)) {
    o_log(O_LOG_WARN, "%s: Couldn't signal read condVar: %s\n",
	  self->name, strerror(errno));
    return 0;
  }
  return 1;
}

/** Remove the oldest object from MTQueue and return a pointer to it.
 *
 * Note, that this returns a pointer to the data stored
 * in the OQueue's internal storage. It is the receiver's
 * responsibility to copy it to other storage if the value
 * needs to be maintained (in other words, subsequent adds
 * to the OQueue may override the returned value.
 *
 * \param queue MTQueue where the string should be added
 * \return a pointer to the data stored or NULL if queue is empty or the type doesn't match
 */
void*
mt_queue_remove(
  MTQueue* queue
) {
  MTQueueInt* self = (MTQueueInt*)queue;
  void* res;

  if (!lock(self)) return 0;
  int rc = 0;
  while(rc != 0 || oqueue_remove_ptr(self->queue, &res) == 0) {
    rc = pthread_cond_wait(&self->readCondVar, &self->mutex);
  }
  unlock(self);

  if (pthread_cond_signal(&self->writeCondVar)) {
    o_log(O_LOG_WARN, "%s: Couldn't signal write condVar: %s\n",
	  self->name, strerror(errno));
    return 0;
  }
  return res;
}


/** Return the oldest object of the OQueue without removing it.
 *
 * \param queue OQueue in which to peek
 * \return object or NULL if queue is empty
 */
void*
mt_queue_peek(
  MTQueue* queue
) {
  MTQueueInt* self = (MTQueueInt*)queue;
  void* res;

  if (!lock(self)) return 0;
  res = oqueue_peek(self->queue);
  unlock(self);
  return res;
}


/** Check if there is still room in the MTQueue.
 *
 * \param queue MTQueue to check
 * \return 1 if empty, 0 otherwise
 */
int
mt_queue_can_add(
  MTQueue* queue
) {
  MTQueueInt* self = (MTQueueInt*)queue;
  int res;

  if (!lock(self)) return 0;
  res = oqueue_can_add(self->queue);
  unlock(self);
  return res;
}

/*. Check if MTQueue is empty.
 * 
 * \param queue MTQueue to check
 * \return 1 if empty, 0 otherwise
 */
int
mt_queue_is_empty(
  MTQueue* queue
) {
  MTQueueInt* self = (MTQueueInt*)queue;
  int res;

  if (!lock(self)) return 0;
  res = oqueue_is_empty(self->queue);
  unlock(self);
  return res;
}

static int
lock(
  MTQueueInt* self
) {
  if (pthread_mutex_lock(&self->mutex)) {
    o_log(O_LOG_WARN, "%s: Couldn't get mutex lock: %s\n",
	  self->name, strerror(errno));
    return 0;
  }
  return 1;
}

static void
unlock(
  MTQueueInt* self
) {
  pthread_mutex_unlock(&self->mutex);
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
