// Used by build.sh when src/samples.c hasn't been generated: the sample-based
// machines (TR-707, TR-505) have no voices and stay silent; everything else
// works. Run scripts/prep_samples.py to generate the real table.
#include <stddef.h>

#include "samples.h"

const Sample* sample_get(int machine, int voice) {
  (void)machine;
  (void)voice;
  return NULL;
}
