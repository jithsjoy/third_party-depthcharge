/*
 * Copyright 2012 Google Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <assert.h>
#include <libpayload.h>

#include "base/cleanup_funcs.h"

ListNode cleanup_funcs;

int run_cleanup_funcs(CleanupType type)
{
	int res = 0;

	CleanupFunc *func;
	list_for_each(func, cleanup_funcs, list_node) {
		assert(func->cleanup);
		if ((func->types & type))
			res = func->cleanup(func, type) || res;
	}

	printf("Exiting depthcharge with code %d at timestamp: %llu\n",
	       type, timer_us(0));

	return res;
}
