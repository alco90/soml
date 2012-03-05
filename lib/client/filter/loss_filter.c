/*
 * Copyright 2012 National ICT Australia (NICTA), Australia
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
/*!\file loss_filter.c
  \brief Implements a filter which counts the losses in a series of sequential integers
*/

#include <stdlib.h>
#include <string.h>
#include <log.h>
#include <oml2/omlc.h>
#include <oml2/oml_filter.h>
#include "filter/loss_filter.h"

typedef struct _omlLossFilterInstanceData InstanceData;

static int
process(OmlFilter* filter, OmlWriter* writer);

static int
sample(OmlFilter* f, OmlValue* values);

void*
omlf_loss_new(
  OmlValueT type,
  OmlValue* result
) {
  InstanceData* self = (InstanceData *)malloc(sizeof(InstanceData));
  
  if (!omlc_is_integer_type(type)) {
    logerror ("Loss filter can only handle integer types\n");
    return NULL;
  }

  if(self) {
    memset(self, 0, sizeof(InstanceData));

    self->result = result;

    self->count = 0;
    self->ooo_count = 0;
    self->sample_count = 0;
    self->last_seen = 0;
    self->uninitialised = 1;

  } else {
    logerror ("Could not allocate %d bytes for loss filter instance data\n",
    sizeof(InstanceData));
    return NULL;
  }

  return self;
}

void
omlf_register_filter_loss (void)
{
  OmlFilterDef def [] =
    {
      { "count", OML_INT32_VALUE },
      { "ooo_count", OML_INT32_VALUE },
      { "sample_count", OML_INT32_VALUE },
      { NULL, 0 }
    };

  omlf_register_filter ("loss",
            omlf_loss_new,
            NULL,
            sample,
            process,
            NULL,
            def);
}

static int
sample(
    OmlFilter* f,
    OmlValue * value  //! values of sample
) {
  InstanceData* self = (InstanceData*)f->instance_data;
  int val;

  if (!omlc_is_integer_type(value->type)) {
    logerror ("Loss filter can only handle integer types\n");
    return -1;
  }

  val = oml_value_to_int(value);

  /* XXX: Does not handle reordering properly,
   * nor sequence space wrapping at all */ 
  if (self->uninitialised) {
    self->uninitialised = 0;
    self->last_seen = val;

  } else if (val < self->last_seen) { /* Don't count duplicates */
    self->ooo_count++;

  } else {
    self->count += val - (self->last_seen + 1);
    self->last_seen = val;
  }
  self->sample_count++;

  return 0;
}

static int
process(
  OmlFilter* f,
  OmlWriter*  writer //! Write results of filter to this function
) {
  InstanceData* self = (InstanceData*)f->instance_data;

  omlc_set_int32(self->result[0].value, self->count);
  omlc_set_int32(self->result[1].value, self->ooo_count);
  omlc_set_int32(self->result[2].value, self->sample_count);

  writer->out(writer, self->result, f->output_count);

  self->count = 0;
  self->ooo_count = 0;
  self->sample_count = 0;

  return 0;
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
