#ifdef WIN32
#include "opensslconfwindows.h"
#elif __APPLE__ && __MACH__
#include "opensslconfmac.h"
#else
#include "opensslconfunix.h"
#endif