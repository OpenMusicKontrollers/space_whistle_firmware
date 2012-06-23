/*
 * Copyright (c) 2012 Hanspeter Portner (agenthp@users.sf.net)
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 
 *     1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 * 
 *     2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 * 
 *     3. This notice may not be removed or altered from any source
 *     distribution.
 */

#ifndef _TUIO2_H_
#define _TUIO2_H_

#include <stdint.h>

#include "nosc/nosc_private.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _Tuio2 Tuio2;

/*
 * (de)alloc
 */
Tuio2 *tuio2_new (uint8_t len);
void tuio2_free (Tuio2 *tuio);

uint16_t tuio2_serialize (Tuio2 *tuio, uint8_t *buf, uint8_t end);

void tuio2_frm_set (Tuio2 *tuio, uint32_t id, nOSC_Timestamp timestamp);
void tuio2_tok_set (Tuio2 *tuio, uint8_t pos, uint32_t S, float x, float p);

#ifdef __cplusplus
}
#endif

#endif