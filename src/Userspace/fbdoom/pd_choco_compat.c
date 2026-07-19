//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//	Small compatibility layer for the vendored chocolate-doom 3.x files
//	(midifile.c/mus2mid.c): helpers that exist in modern chocolate-doom
//	but not in the older fbDOOM base tree. SDL_SwapBE* are what 3.x's
//	i_swap.h resolves byte swapping to; here they are plain functions
//	(this port is little-endian x86_64 only, so BE swaps always swap).
//

#include <stdio.h>
#include <stdlib.h>

#include "doomtype.h"
#include "i_system.h"

unsigned short SDL_SwapBE16(unsigned short x)
{
    return (unsigned short)((x >> 8) | (x << 8));
}

unsigned int SDL_SwapBE32(unsigned int x)
{
    return (x >> 24) | ((x >> 8) & 0x0000ff00u)
         | ((x << 8) & 0x00ff0000u) | (x << 24);
}

void *I_Realloc(void *ptr, size_t size)
{
    void *new_ptr = realloc(ptr, size);

    if (size != 0 && new_ptr == NULL)
    {
        I_Error("I_Realloc: failed on reallocation of %lu bytes",
                (unsigned long)size);
    }

    return new_ptr;
}

FILE *M_fopen(const char *filename, const char *mode)
{
    return fopen(filename, mode);
}
