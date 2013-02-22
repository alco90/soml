/*
 * Copyright 2007-2013 National ICT Australia (NICTA), Australia
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
/** \file api.c
 * Implement the user-visible API of OML.
 */

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>

#include "oml2/oml_filter.h"
#include "oml2/omlc.h"
#include "ocomm/o_log.h"
#include "oml_value.h"
#include "validate.h"
#include "client.h"

static void omlc_ms_process(OmlMStream* ms);
static void omlc_ms_send_metadata(OmlMStream *ms, const char *key, const OmlValueU *value, OmlValueT type);

/** DEPRECATED
 * \see omlc_inject
 */
void
omlc_process(OmlMP *mp, OmlValueU *values)
{
  logwarn("'omlc_process' is deprecated, use 'omlc_inject' instead\n");
  omlc_inject(mp, values);
}

/**  Inject a measurement sample into a Measurement Point.
 *
 * \param mp pointer to OmlMP into which the new sample is being injected
 * \param values an array of OmlValueU to be processed
 * \return 0 on success, <0 otherwise
 *
 * The values' types is assumed to be the same as what was passed to omlc_add_mp
 * Type information is stored in (stored in mp->param_defs[].param_types)
 *
 * Traverse the list of MSs attached to this MP and, for each MS, the list of
 * filters to apply to the sample. Input the relevant field of the MP to each
 * filter, the call omlc_ms_process() to determine whether a new sample has to
 * be output on that MS.
 *
 * The content of values is deep-copied into the MSs' storage, so values can be
 * directly freed/reused when inject returns.
 *
 * \see omlc_add_mp, omlc_ms_process, oml_value_set
 */
int
omlc_inject(OmlMP *mp, OmlValueU *values)
{
  OmlMStream* ms;
  OmlValue v;

  if (omlc_instance == NULL) return -1;
  if (mp == NULL || values == NULL) return -1;

  oml_value_init(&v);

  if (mp_lock(mp) == -1) {
    logwarn("Cannot lock MP '%s' for injection\n", mp->name);
    return -1;
  }
  ms = mp->streams;
  while (ms) {
    OmlFilter* f = ms->filters;
    for (; f != NULL; f = f->next) {

      /* FIXME:  Should validate this indexing */
      oml_value_set(&v, &values[f->index], mp->param_defs[f->index].param_types);

      f->input(f, &v);
    }
    omlc_ms_process(ms);
    ms = ms->next;
  }
  mp_unlock(mp);
  oml_value_reset(&v);

  return 0;
}

/** Inject metadata (key/value) for a specific MP.
 *
 * With the current storage backends, the key will be a concatenation following
 * this pattern: MPNAME_[FIELDNAME_]KEY. This transformation is done on the
 * client's side. Additionally any later injection of metadata in an already
 * existing key will override its provious value.
 *
 * \param mp pointer to the OmlMP to which the metadata relates
 * \param key base name for the key (keys are unique)
 * \param value OmlValueU containing the value for the given key
 * \param type OmlValueT of value, currently only OML_STRING_VALUE is valid
 * \param fname optional field name to which this metadata relates
 * \return 0 on success, -1 otherwise
 *
 * \see omlc_add_mp
 */
int
omlc_inject_metadata(OmlMP *mp, const char *key, const OmlValueU *value, OmlValueT type, const char *fname)
{
  char *fullkey;
  int len, len2;
  int i, ret = -1;

  OmlMPDef *f;
  OmlMStream *ms;

  if (omlc_instance == NULL) {
    logerror("Cannot inject metadata until omlc_start has been called\n");

  } else if (!mp || !key || !value) {
    logwarn("Trying to inject metadata with missing mp, key and/or value\n");

  } else if (! validate_name(key)) {
    logerror("%s is an invalid metadata key name\n", key);

  } else if (type != OML_STRING_VALUE) {
    logwarn("Currently, only OML_STRING_VALUE are valid as metadata value\n");

  } else {
    assert(mp->name);

    len = strlen(key);
    if (fname) {
      /* Make sure fname exists in this MP */
      /* XXX: This should probably be done in another function (returning the OmlMPDef? */
      f = mp->param_defs;
      loginfo("%s %d\n", fname, mp->param_count);
      for (i = 0; (i < mp->param_count) && strcmp(f[i].name, fname); i++) {
        loginfo("%s\n", f[i].name);
      }
      if (i >= mp->param_count) {
        logerror("Field %s not found in MP %s\n", fname, mp->name);
        return -1; /* XXX: It would be too messy to try to get to the end return from here */
      }

      len += strlen(fname) + 1; /* '_' */
    }
    len += strlen(mp->name) + 1; /* '_' */

    fullkey = xmalloc(len + 1);
    if (!fullkey) {
      logerror("Cannot allocate memory for full key name\n");

    } else {
      len2 = snprintf(fullkey, len + 1, "%s_", mp->name);
      if (fname) {
        len2 += snprintf(fullkey + len2, len + 1 - len2, "%s_", fname);
      }
      len2 += snprintf(fullkey + len2, len + 1 - len2, "%s", key);
      assert(len2 == len);

      if (mp_lock(mp) == -1) {
        logwarn("Cannot lock MP '%s' for metadata injection\n", mp->name);
      } else {
        ms = mp->streams;
        while (ms) {
          /* Send the meta data along with all streams
           *
           * XXX: This might create duplicates, but it's ok for now as old
           * value get overwritten in the DB.
           */
          omlc_ms_send_metadata(ms, fullkey, value, type);
          ms = ms->next;
        }
        mp_unlock(mp);

        ret = 0;
      }

      xfree(fullkey);
    }
  }

  return ret;
}

/** Called when the particular MS has been filled.
 *
 * Determine whether a new sample must be issued (in per-sample reporting), and
 * ask the filters to generate it if need be.
 *
 * A lock for the MP containing that MS must be held before calling this function.
 *
 * \param ms pointer to the OmlMStream to process
 * \see filter_process
 */
static void
omlc_ms_process(OmlMStream *ms)
{
  if (ms == NULL) return;

  if (ms->sample_thres > 0 && ++ms->sample_size >= ms->sample_thres) {
    // sample based filters fire
    filter_process(ms);
  }

}

/** Send some key/value metadata along on the given MS
 *
 * A lock for the MP containing that MS must be held before calling this
 * function.
 *
 * Metadata is sent using schema 0, which has a key/value schema with two
 * strings.
 *
 * TODO: Make this more generic by instantiating this schema by default for all
 * MSs.
 *
 * \param ms pointer to the OmlMStream to send data over
 * \param key ditto
 * \param value OmlValueU containing the value
 * \see filter_process
 */
static void
omlc_ms_send_metadata(OmlMStream *ms, const char *key, const OmlValueU *value, OmlValueT type)
{
  struct timeval tv;
  double now;
  OmlWriter* writer;
  OmlValue keyval[2];
  OmlMStream mdms;

  if (ms == NULL) return;

  oml_value_array_init(keyval, 2);

  /* XXX: Until the TODO above is addressed, we need to do that here. */
  memset(&mdms, 0, sizeof(OmlMStream));
  mdms.index = 0;
  mdms.seq_no = ++ms->meta_seq_no;

  writer = ms->writer;

  /* From lib/client/filter.c:filter_process
   * XXX: This should probably be factored into a more generic function called
   * both by this function and filter_process
   */
  gettimeofday(&tv, NULL);
  now = tv.tv_sec - omlc_instance->start_time + 0.000001 * tv.tv_usec;


  oml_value_set_type(&keyval[0], OML_STRING_VALUE);
  oml_value_from_s(&keyval[0], key);
  oml_value_set(&keyval[1], value, type);

  writer->row_start(writer, &mdms, now);
  writer->out(writer, keyval, 2);
  writer->row_end(writer, &mdms);

  oml_value_reset(keyval);
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
