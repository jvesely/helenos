/*
 * Copyright (c) 2010 Lenka Trochtova
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup devman
 * @{
 */

#include <str.h>

#include "devman.h"

int get_match_score(driver_t *drv, node_t *dev)
{
	link_t *drv_head = &drv->match_ids.ids;
	link_t *dev_head = &dev->match_ids.ids;
	
	if (list_empty(drv_head) || list_empty(dev_head))
		return 0;
	
	/*
	 * Find first matching pair.
	 */
	link_t *drv_link = drv->match_ids.ids.next;
	while (drv_link != drv_head) {
		link_t *dev_link = dev->match_ids.ids.next;
		while (dev_link != dev_head) {
			match_id_t *drv_id = list_get_instance(drv_link, match_id_t, link);
			match_id_t *dev_id = list_get_instance(dev_link, match_id_t, link);

			if (str_cmp(drv_id->id, dev_id->id) == 0) {
				/*
				 * We found a match.
				 * Return the score of the match.
				 */
				return drv_id->score * dev_id->score;
			}

			dev_link = dev_link->next;
		}
		drv_link = drv_link->next;
	}
	
	return 0;
}

/** @}
 */
