/* Exposes the DOL module's generated dispatcher under a unique name.
   Each generated module defines the same static inline helpers, so every
   module gets its own wrapper translation unit. */
#include "SMNE01.h"

DolRecompFunction nsmbw_find_dol(u32 address) {
    return dolrecomp_find_original(address);
}
