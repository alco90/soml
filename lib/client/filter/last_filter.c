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
/*!\file last_filter.c
  \brief Implements a filter which captures the last value presented
*/

#include <stdlib.h>
#include <string.h>
#include <log.h>
#include <oml2/omlc.h>
#include <oml2/oml_filter.h>
#include "oml_value.h"
#include "filter/last_filter.h"

typedef struct _omlLastFilterInstanceData InstanceData;

static int
process(OmlFilter* filter, OmlWriter* writer);

static int
sample(OmlFilter* f, OmlValue* values);

void*
omlf_last_new(
  OmlValueT type,
  OmlValue* result
) {
  InstanceData* self = (InstanceData *)xmalloc(sizeof(InstanceData));
  if(self) {
    memset(self, 0, sizeof(InstanceData));

    self->result = result;
    self->result[0].type = type;  // FIXME:  Is this needed?

    if (type == OML_STRING_VALUE)
      omlc_set_const_string(*oml_value_get_value(&self->result[0]), "");

  } else {
    logerror ("Could not allocate %d bytes for last filter instance data\n",
    sizeof(InstanceData));
    return NULL;
  }

  return self;
}

void
omlf_register_filter_last (void)
{
  OmlFilterDef def [] =
    {
      { "last", OML_INPUT_VALUE },
      { NULL, 0 }
    };

  omlf_register_filter ("last",
            omlf_last_new,
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
  OmlValueU* v = oml_value_get_value(value);;
  OmlValueT type = oml_value_get_type(value);;

  if (type != self->result[0].type) {
    logerror ("Different type from initial definition\n");
    return 0;
  }
  /* Overwrite previously stored value */
  return oml_value_set(&self->result[0], v, type);
}

static int
process(
  OmlFilter* f,
  OmlWriter*  writer //! Write results of filter to this function
) {
  InstanceData* self = (InstanceData*)f->instance_data;

  writer->out(writer, self->result, f->output_count);
  oml_value_reset(&self->result[0]);
  if (self->result[0].type == OML_STRING_VALUE)
    omlc_set_const_string (*oml_value_get_value(&self->result[0]), "");
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
