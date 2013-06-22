//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2000 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_F_CONVOLUTE_H
#define f_F_CONVOLUTE_H

#include <windows.h>

#include "filter.h"

struct ConvoluteFilterData {
	long m[9];
	long bias;
	void *dyna_func;
	DWORD dyna_size;
	DWORD dyna_old_protect;
	BOOL fClip;
};

int filter_convolute_run(const FilterActivation *fa, const FilterFunctions *ff);
long filter_convolute_param(FilterActivation *fa, const FilterFunctions *ff);
int filter_convolute_end(FilterActivation *fa, const FilterFunctions *ff);
int filter_convolute_start(FilterActivation *fa, const FilterFunctions *ff);

#endif