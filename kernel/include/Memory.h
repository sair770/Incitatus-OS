/**
| Copyright(C) 2012 Ali Ersenal
| License: WTFPL v2
| URL: http://sam.zoy.org/wtfpl/COPYING
|
|--------------------------------------------------------------------------
| Memory.h
|--------------------------------------------------------------------------
|
| DESCRIPTION:  Provides functions which modify memory.
|
| AUTHOR:       Ali Ersenal, aliersenal@gmail.com
\------------------------------------------------------------------------*/


#ifndef MEMORY_H
#define MEMORY_H

#include <Common.h>

/*-------------------------------------------------------------------------
| Set memory
|--------------------------------------------------------------------------
| DESCRIPTION:     Sets every single byte in specified memory
|                  space of length "length" to "valueToSet".
|
| PARAM:           "ptrToMem"    pointer to memory space
|                  "valueToSet"  new byte value
|                  "length"      number of bytes to be modified
\------------------------------------------------------------------------*/
void  Memory_set(void* ptrToMem, char valueToSet, u32int length);

/*-------------------------------------------------------------------------
| Copy memory
|--------------------------------------------------------------------------
| DESCRIPTION:     Copies "sourceLength" bytes from "source" to
|                  "destination".
|
| PARAM:           "destination"    pointer to destination memory
|                  "source"         pointer to source memory
|                  "sourceLength"   size of source
|
| PRECONDITION:    "destination" and "source" should not overlap
\------------------------------------------------------------------------*/
void  Memory_copy(void* destination, const void* source, u32int sourceLength);
#endif
