/*
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2005 Richard Wilson <info@tinct.net>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Interface to HTML table processing and layout.
 */

#ifndef NETSURF_HTML_TABLE_H
#define NETSURF_HTML_TABLE_H

#include <stdbool.h>

struct box;

bool table_calculate_column_types(
		const nscss_len_ctx *len_ctx,
		struct box *table);
void table_used_border_for_cell(
		const nscss_len_ctx *len_ctx,
		struct box *cell);

#endif
