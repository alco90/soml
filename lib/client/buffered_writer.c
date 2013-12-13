/*
 * Copyright 2011-2013 National ICT Australia (NICTA)
 *
 * This software may be used and distributed solely under the terms of
 * the MIT license (License).  You should find a copy of the License in
 * COPYING or at http://opensource.org/licenses/MIT. By downloading or
 * using this software you accept the terms and the liability disclaimer
 * in the License.
 */
/** \file buffered_writer.c
 * \brief A non-blocking, self-draining FIFO queue using threads.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "oml2/omlc.h"
#include "ocomm/o_log.h"
#include "ocomm/o_socket.h"

#include "client.h"
#include "buffered_writer.h"

/** Default target size in each MBuffer of the chunk */
#define DEF_CHAIN_BUFFER_SIZE 1024

/** A chunk of data to be put in a circular chain */
typedef struct BufferChunk {

  /** Link to the next buffer in the chunk */
  struct BufferChunk*  next;

  /** MBuffer used for storage */
  MBuffer* mbuf;
  /** Target maximal size of mbuf for that chunk */
  size_t   targetBufSize;

  /** Set to 1 when the reader is processing this chunk */
  /* XXX: This really should be a mutex */
  int   reading;
} BufferChunk;

/** A writer reading from a chain of BufferChunks */
typedef struct BufferedWriter {
  /** Set to !0 if buffer is active; 0 kills the thread */
  int  active;

  /** Number of links which can still be allocated */
  long unallocatedBuffers;
  /** Target size of MBuffer in each chunk*/
  size_t bufSize;

  /** Opaque handler to  the output stream*/
  OmlOutStream*    outStream;

  /** Chunk where the data gets stored until it's pushed out */
  BufferChunk* writerChunk;
  /** Immutable entry into the chain */
  BufferChunk* firstChunk;

  /** Buffer holding protocol headers */
  MBuffer*     meta_buf;

  /** Mutex protecting the object */
  pthread_mutex_t lock;
  /** Semaphore for this object */
  pthread_cond_t semaphore;
  /** Thread in charge of reading the queue and writing the data out */
  pthread_t  readerThread;

  /** Time of the last failure, to backoff for REATTEMP_INTERVAL before retryying **/
  time_t last_failure_time;

  /** Backoff time, in seconds */
  uint8_t backoff;

} BufferedWriter;
#define REATTEMP_INTERVAL 5    //! Seconds to open the stream again

static BufferChunk* getNextWriteChunk(BufferedWriter* self, BufferChunk* current);
static BufferChunk* createBufferChunk(BufferedWriter* self);
static int destroyBufferChain(BufferedWriter* self);
static void* threadStart(void* handle);
static BufferChunk* processChunk(BufferedWriter* self, BufferChunk* chunk);

/** Create a BufferedWriter instance
 *
 * \param outStream opaque OmlOutStream handler
 * \param queueCapacity maximal size [B] of the internal queue queueCapaity/chunkSize will be used (at least 2)
 * \param chunkSize size [B] of buffer space allocated at a time, set to 0 for default (DEF_CHAIN_BUFFER_SIZE)
 * \return an instance pointer if successful, NULL otherwise
 *
 * \see DEF_CHAIN_BUFFER_SIZE
 */
BufferedWriterHdl
bw_create(OmlOutStream* outStream, long  queueCapacity, long chunkSize)
{
  long nchunks;
  BufferedWriter* self = NULL;

  assert(outStream>=0);
  assert(queueCapacity>=0);
  assert(chunkSize>=0);

  if((self = (BufferedWriter*)oml_malloc(sizeof(BufferedWriter)))) {
    memset(self, 0, sizeof(BufferedWriter));

    self->outStream = outStream;
    /* This forces a 'connected' INFO message upon first connection */
    self->backoff = 1;

    self->bufSize = chunkSize > 0 ? chunkSize : DEF_CHAIN_BUFFER_SIZE;

    nchunks = queueCapacity / self->bufSize;
    self->unallocatedBuffers = (nchunks > 2) ? nchunks : 2; /* at least two chunks */

    logdebug ("%s: Buffer size %dB (%d chunks of %dB)\n",
        self->outStream->dest,
        self->unallocatedBuffers*self->bufSize,
        self->unallocatedBuffers, self->bufSize);

    if(NULL == (self->writerChunk = self->firstChunk = createBufferChunk(self))) {
      oml_free(self);
      self = NULL;

    } else if(NULL == (self->meta_buf = mbuf_create())) {
      destroyBufferChain(self);
      oml_free(self);
      self = NULL;

    } else {
      /* Initialize mutex and condition variable objects */
      pthread_cond_init(&self->semaphore, NULL);
      pthread_mutex_init(&self->lock, NULL);

      /* Initialize and set thread detached attribute */
      pthread_attr_t tattr;
      pthread_attr_init(&tattr);
      pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
      self->active = 1;
      pthread_create(&self->readerThread, &tattr, threadStart, (void*)self);
    }
  }

  return (BufferedWriterHdl)self;
}

/** Close an output stream and destroy the objects.
 *
 * \param instance handle (i.e., pointer) to a BufferedWriter
 */
void
bw_close(BufferedWriterHdl instance)
{
  BufferedWriter *self = (BufferedWriter*)instance;

  if(!self) { return; }

  if (oml_lock (&self->lock, __FUNCTION__)) { return; }
  self->active = 0;

  loginfo ("%s: Waiting for buffered queue thread to drain...\n", self->outStream->dest);

  pthread_cond_signal (&self->semaphore);
  oml_unlock (&self->lock, __FUNCTION__);
  switch (pthread_join (self->readerThread, NULL)) {
  case 0:
    logdebug ("%s: Buffered queue reader thread finished OK...\n", self->outStream->dest);
    break;
  case EINVAL:
    logerror ("%s: Buffered queue reader thread is not joinable\n", self->outStream->dest);
    break;
  case EDEADLK:
    logerror ("%s: Buffered queue reader thread shutdown deadlock, or self-join\n", self->outStream->dest);
    break;
  case ESRCH:
    logerror ("%s: Buffered queue reader thread shutdown failed: could not find the thread\n", self->outStream->dest);
    break;
  default:
    logerror ("%s: Buffered queue reader thread shutdown failed with an unknown error\n", self->outStream->dest);
    break;
  }

  self->outStream->close(self->outStream);
  destroyBufferChain(self);
  oml_free(self);
}

/** Add some data to the end of the queue.
 *
 * This function tries to acquire the lock on the BufferedWriter, and releases
 * it when done.
 *
 * \param instance BufferedWriter handle
 * \param data Pointer to data to add
 * \param size size of data
 * \return 0 on success, -1 on error
 *
 * \see _bw_push
 */
int
bw_push(BufferedWriterHdl instance, uint8_t *data, size_t size)
{
  int result = 0;
  BufferedWriter* self = (BufferedWriter*)instance;
  if (0 == oml_lock(&self->lock, __FUNCTION__)) {
    result =_bw_push(instance, data, size);
    oml_unlock(&self->lock, __FUNCTION__);
  }
  return result;
}

/** Add some data to the end of the queue (lock must be held).
 *
 * This function is the same as bw_push except it assumes that the
 * lock is already acquired.
 *
 * \copydetails bw_push
 *
 * \see bw_push
 */
int
_bw_push(BufferedWriterHdl instance, uint8_t* data, size_t size)
{
  BufferedWriter* self = (BufferedWriter*)instance;
  if (!self->active) { return -1; }

  BufferChunk* chunk = self->writerChunk;
  if (chunk == NULL) { return -1; }

  if (mbuf_wr_remaining(chunk->mbuf) < size) {
    chunk = self->writerChunk = getNextWriteChunk(self, chunk);
  }

  if (mbuf_write(chunk->mbuf, data, size) < 0) {
    return -1;
  }

  pthread_cond_signal(&self->semaphore);

  return 0;
}

/** Add some data to the end of the header buffer.
 *
 * This function tries to acquire the lock on the BufferedWriter, and releases
 * it when done.
 *
 * \param instance BufferedWriter handle
 * \param data Pointer to data to add
 * \param size size of data
 * \return 0 on success, -1 on error
 *
 * \see _bw_push_meta
 */
int
bw_push_meta(BufferedWriterHdl instance, uint8_t* data, size_t size)
{
  int result = 0;
  BufferedWriter* self = (BufferedWriter*)instance;
  if (0 == oml_lock(&self->lock, __FUNCTION__)) {
    result = _bw_push_meta(instance, data, size);
    oml_unlock(&self->lock, __FUNCTION__);
  }
  return result;
}

/** Add some data to the end of the header buffer (lock must be held).
 *
 * This function is the same as bw_push_meta except it assumes that the lock is
 * already acquired.
 *
 * \copydetails bw_push_meta
 *
 * \see bw_push_meta
 *
 */
int
_bw_push_meta(BufferedWriterHdl instance, uint8_t* data, size_t size)
{
  BufferedWriter* self = (BufferedWriter*)instance;
  int result = -1;

  if (!self->active) { return -1; }

  if (mbuf_write(self->meta_buf, data, size) > 0) {
    result = 0;
    /* XXX: There is no point in signalling the semaphore as the
     * writer will not be able to do anything with the new data.
     *
     * Also, it puts everything in a deadlock otherwise */
    /* pthread_cond_signal(&self->semaphore); */
  }
  return result;
}

/** Return an MBuffer with (optional) exclusive write access
 *
 * If exclusive access is required, the caller is in charge of releasing the
 * lock with bw_unlock_buf.
 *
 * \param instance BufferedWriter handle
 * \param exclusive indicate whether the entire BufferedWriter should be locked
 *
 * \return an MBuffer instance if success to write in, NULL otherwise
 * \see bw_unlock_buf
 */
MBuffer*
bw_get_write_buf(BufferedWriterHdl instance, int exclusive)
{
  MBuffer* mbuf = NULL;
  BufferChunk* chunk = NULL;

  assert(instance);
  BufferedWriter* self = (BufferedWriter*)instance;

  if (oml_lock(&self->lock, __FUNCTION__)) {
    logdebug("%s: Cannot acquire lock to \n", self->outStream->dest);
    return NULL;

  } else if (!self->active) {
    logdebug("%s: Writer inactive, cannot write anymore\n", self->outStream->dest);

  } else if ((chunk = self->writerChunk)) {
    mbuf = chunk->mbuf;
    assert(mbuf);

    if (mbuf_write_offset(mbuf) >= chunk->targetBufSize) {
      chunk = self->writerChunk = getNextWriteChunk(self, chunk);
      mbuf = chunk->mbuf;
    }

  }

  if (!exclusive || !mbuf) { /* Don't hold the lock if no write buffer was found */
    oml_unlock(&self->lock, __FUNCTION__);
  }
  return mbuf;
}


/** Return and unlock MBuffer
 * \param instance BufferedWriter handle for which a buffer was previously obtained through bw_get_write_buf
 *
 * \see bw_get_write_buf
 */
void
bw_unlock_buf(BufferedWriterHdl instance)
{
  BufferedWriter* self = (BufferedWriter*)instance;
  pthread_cond_signal(&self->semaphore); /* assume we locked for a reason */
  oml_unlock(&self->lock, __FUNCTION__);
}

/** Find the next empty write chunk, sets self->writerChunk to it and returns it.
 *
 * We only use the next one if it is empty. If not, we essentially just filled
 * up the last chunk and wrapped around to the socket reader. In that case, we
 * either create a new chunk if the overall buffer can still grow, or we drop
 * the data from the current one.
 *
 * This assumes that the current thread holds the self->lock and the lock on
 * the self->writerChunk.
 *
 * \param self BufferedWriter pointer
 * \param current BufferChunk to use or from which to find the next
 * \return a BufferChunk in which data can be stored
 */
BufferChunk*
getNextWriteChunk(BufferedWriter* self, BufferChunk* current) {
  assert(current != NULL);
  BufferChunk* nextBuffer = current->next;
  assert(nextBuffer != NULL);

  BufferChunk* resChunk = NULL;
  if (mbuf_rd_remaining(nextBuffer->mbuf) == 0) {
    // It's empty (the reader has finished with it), we can use it
    mbuf_clear2(nextBuffer->mbuf, 0);
    resChunk = nextBuffer;

  } else if (self->unallocatedBuffers > 0) {
    // Insert a new chunk between current and next one.
    BufferChunk* newBuffer = createBufferChunk(self);
    assert(newBuffer);
    newBuffer->next = nextBuffer;
    current->next = newBuffer;
    resChunk = newBuffer;

  } else {
    // The chain is full, time to drop data and reuse the next buffer
    current = nextBuffer;
    assert(nextBuffer->reading == 0); /* Ensure this is not the chunk currently being read */
    logwarn("Dropping %d bytes of measurement data\n", mbuf_fill(nextBuffer->mbuf));
    mbuf_repack_message2(nextBuffer->mbuf);
    resChunk = nextBuffer;
  }
  // Now we just need to copy the message from current to resChunk
  int msgSize = mbuf_message_length(current->mbuf);
  if (msgSize > 0) {
    mbuf_write(resChunk->mbuf, mbuf_message(current->mbuf), msgSize);
    mbuf_reset_write(current->mbuf);
  }
  return resChunk;
}

/** Initialise a BufferChunk for a BufferedWriter.
 * \param self BufferedWriter pointer
 * \return a pointer to the newly-created BufferChunk, or NULL on error
 */
BufferChunk*
createBufferChunk(BufferedWriter* self)
{
  size_t initsize = 0.1 * self->bufSize;
  MBuffer* buf = mbuf_create2(self->bufSize, initsize);
  if (buf == NULL) { return NULL; }

  BufferChunk* chunk = (BufferChunk*)oml_malloc(sizeof(BufferChunk));
  if (chunk == NULL) {
    mbuf_destroy(buf);
    return NULL;
  }
  memset(chunk, 0, sizeof(BufferChunk));

  // set state
  chunk->mbuf = buf;
  chunk->targetBufSize = self->bufSize;
  chunk->next = chunk;

  self->unallocatedBuffers--;
  logdebug("Allocated chunk of size %dB (up to %d), %d remaining\n",
        initsize, self->bufSize, self->unallocatedBuffers);
  return chunk;
}


/** Destroy the Buffer chain of a BufferedWriter
 *
 * \param self pointer to the BufferedWriter
 *
 * \return 0 on success, or a negative number otherwise
 */
int
destroyBufferChain(BufferedWriter* self) {
  BufferChunk *chunk, *start;

  if (!self) {
    return -1;
  }

  /* BufferChunk is a circular buffer */
  start = self->firstChunk;
  while( (chunk = self->firstChunk) && chunk!=start) {
    logdebug("Destroying BufferChunk at %p\n", chunk);
    self->firstChunk = chunk->next;

    mbuf_destroy(chunk->mbuf);
    oml_free(chunk);
  }

  pthread_cond_destroy(&self->semaphore);
  pthread_mutex_destroy(&self->lock);

  return 0;
}


/** Writing thread
 *
 * \param handle the stream to use the filters on
 * \return NULL
 */
static void*
threadStart(void* handle)
{
  BufferedWriter* self = (BufferedWriter*)handle;
  BufferChunk* chunk = self->firstChunk, *next_chunk = NULL;

  while (self->active) {
    oml_lock(&self->lock, "bufferedWriter");
    pthread_cond_wait(&self->semaphore, &self->lock);

    /* Process all chunks which have data in them, stop when we caught up to
     * the writer, or when a soft (e.g. no data sent) or hard (e.g., cannot
     * resolve) error occurred.
     */
    do {
      next_chunk = processChunk(self, chunk);
      if (next_chunk && next_chunk != chunk) {
        chunk = next_chunk;
      } else {
        break;
      }
    } while(chunk != self->writerChunk);
    oml_unlock(&self->lock, "bufferedWriter");
  }

  /* Drain this writer before terminating */
  do {
    next_chunk = processChunk(self, chunk);
    if (next_chunk && next_chunk != chunk) {
      chunk = next_chunk;
    } else {
      break;
    }
  } while(chunk != self->writerChunk);

  return NULL;
}

/** Send data contained in one chunk.
 *
 * \param self BufferedWriter to process
 * \param chunk link of the chunk to process (can be NULL)
 *
 * \return a pointer to the next chunk to process (can be chunk in case of failure), or NULL on error
 * \see oml_outs_write_f
 */
static BufferChunk*
processChunk(BufferedWriter* self, BufferChunk* chunk)
{
  assert(self);
  assert(self->meta_buf);

  if(!chunk) {
    return NULL;
  }
  assert(chunk->mbuf);

  uint8_t* buf = mbuf_rdptr(chunk->mbuf);
  size_t size = mbuf_message_offset(chunk->mbuf) - mbuf_read_offset(chunk->mbuf);
  size_t sent = 0;

      if (mbuf_message(chunk->mbuf) > mbuf_rdptr(chunk->mbuf)) {
      }

  MBuffer* meta = self->meta_buf;

  /* XXX: Should we use a timer instead? */
  time_t now;
  time(&now);
  if (difftime(now, self->last_failure_time) < self->backoff) {
    logdebug("%s: Still in back-off period (%ds)\n", self->outStream->dest, self->backoff);
    return chunk;
  }

  chunk->reading = 1;

  while (size > sent) {
    long cnt = self->outStream->write(self->outStream, (void*)(buf + sent), size - sent,
                               meta->rdptr, meta->fill);
    if (cnt > 0) {
      sent += cnt;
      if (self->backoff) {
        self->backoff = 0;
        loginfo("%s: Connected\n", self->outStream->dest);
      }

    } else if (cnt == 0) {
      logdebug("%s: Did not send anything\n", self->outStream->dest);
      chunk->reading = 0;
      return chunk;

    } else if(self->backoff && !self->active) {
      logwarn("%s: Error sending while draining queue; giving up...\n", self->outStream->dest);
      chunk->reading = 0;
      return NULL;

    } else {
      /* To be on the safe side, we rewind to the beginning of the
       * chunk and try to resend everything - this is especially important
       * if the underlying stream needs to reopen and resync. */
      mbuf_reset_read(chunk->mbuf);
      size = mbuf_message_offset(chunk->mbuf) - mbuf_read_offset(chunk->mbuf);
      sent = 0;
      self->last_failure_time = now;
      if (!self->backoff) {
        self->backoff = 1;
      } else if (self->backoff < UINT8_MAX) {
        self->backoff *= 2;
      }
      logwarn("%s: Error sending, backing off for %ds\n", self->outStream->dest, self->backoff);

      chunk->reading = 0;
      return chunk;
    }
  }

  /* Check that we have sent everything, and reset the MBuffer
   * XXX: is this really needed? size>sent *should* be enough
   */
  mbuf_read_skip(chunk->mbuf, sent);
  chunk->reading = 0;
  if (mbuf_write_offset(chunk->mbuf) == mbuf_read_offset(chunk->mbuf)) {
    mbuf_clear2(chunk->mbuf, 1);
    return chunk->next;
  }
  return chunk;
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
