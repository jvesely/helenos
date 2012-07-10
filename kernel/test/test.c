/*
 * Copyright (c) 2006 Martin Decky
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

/** @addtogroup test
 * @{
 */
/** @file
 */

#include <test.h>

bool test_quiet;

test_t tests[] = {
#include <atomic/atomic1.def>
#include <avltree/avltree1.def>
#include <btree/btree1.def>
#include <debug/mips1.def>
#include <fault/fault1.def>
#include <mm/falloc1.def>
#include <mm/falloc2.def>
#include <mm/mapping1.def>
#include <mm/slab1.def>
#include <mm/slab2.def>
#include <synch/semaphore1.def>
#include <synch/semaphore2.def>
#include <synch/rcu1.def>
#include <synch/workqueue2.def>
#include <synch/workqueue3.def>
#include <print/print1.def>
#include <print/print2.def>
#include <print/print3.def>
#include <print/print4.def>
#include <print/print5.def>
#include <thread/thread1.def>
#include <smpcall/smpcall1.def>
	{
		.name = NULL,
		.desc = NULL,
		.entry = NULL
	}
};

/** @}
 */
