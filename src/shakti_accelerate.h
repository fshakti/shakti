#ifndef SHAKTI_ACCELERATE_H
#define SHAKTI_ACCELERATE_H

#if SHAKTI_USE_ACCELERATE
/* a.h short-name macros collide with Apple Accelerate / vecLib headers. */
#undef ia
#undef it
#undef ih
#undef ii
#undef ij
#undef ik
#undef il
#undef im
#undef in
#undef cc
#undef cd
#undef ss
#undef st
#include <Accelerate/Accelerate.h>
#endif

#endif
