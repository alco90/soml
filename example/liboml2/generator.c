/*
 * This file was automatically generated by oml2-scaffold 2.10.0
 * for generator version 1.0.0.
 * Please edit to suit your needs; the run() function should contain application code.
 */
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#define USE_OPTS
#include <popt.h>
#include "generator_popt.h"

#include <oml2/omlc.h>
#define OML_FROM_MAIN
#include "generator_oml.h"

#include "config.h"

/* Do application-specific work here */
void run(opts_t* opts, oml_mps_t* oml_mps)
{
  double angle = 0;
  double delta = opts->frequency * opts->sample_interval * 2 * M_PI;
  unsigned long sleep = (unsigned long)(opts->sample_interval * 1E6);

  printf("%f, %f, %ld\n", M_PI, delta, sleep);

  int i = opts->samples;
  unsigned int count = 1;
  for (; i != 0; i--, count++) {
    char label[64];
    sprintf(label, "sample-%d", count);

    oml_inject_d_lin(oml_mps->d_lin, label, count);

    double value = opts->amplitude * sin(angle);
    oml_inject_d_sin(oml_mps->d_sin, label, angle, value);

    printf("%s %d | %f %f\n", label, count, angle, value);

    angle = fmodf(angle + delta, 2 * M_PI);
    usleep(sleep);
  }
}

int main(int argc, const char *argv[])
{
  int c;

  omlc_init("generator", &argc, argv, NULL);

  /* Parse command line arguments */
  poptContext optCon = poptGetContext(NULL, argc, argv, options, 0); /* options is defined in generator_popt.h */
  while ((c = poptGetNextOpt(optCon)) > 0) {}

  /* Initialise measurement points */
  oml_register_mps(); /* Defined in generator_oml.h */
  omlc_start();

  run(g_opts, g_oml_mps_generator); /* Do some work and injections, see above */

  omlc_close();

  return(0);
}

/*
 Local Variables:
 mode: C
 tab-width: 2
 indent-tabs-mode: nil
 End:
 vim: sw=2:sts=2:expandtab
*/
