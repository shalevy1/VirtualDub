//	VirtualDub - Video processing and capture application
//	Graphics support library
//	Copyright (C) 1998-2007 Avery Lee
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

#ifndef f_VD2_KASUMI_TEXT_H
#define f_VD2_KASUMI_TEXT_H

struct VDOutlineFontGlyphInfo {
	uint16	mPointArrayStart;		// start of points (encoded as 8:8)
	uint16	mCommandArrayStart;		// start of commands (encoded as 6:2 RLE).
	sint16	mAWidth;				// advance from start to character cell
	sint16	mBWidth;				// width of character cell
	sint16	mCWidth;				// advance from character cell to end
};

struct VDOutlineFontInfo {
	const uint16 *mpPointArray;
	const uint8 *mpCommandArray;
	const VDOutlineFontGlyphInfo *mpGlyphArray;
	int		mStartGlyph;
	int		mEndGlyph;
	int		mMinX;
	int		mMinY;
	int		mMaxX;
	int		mMaxY;
	int		mEmSquare;
	int		mAscent;
	int		mDescent;
	int		mLineGap;
};

void VDPixmapConvertTextToPath(VDPixmapPathRasterizer& rast, const VDOutlineFontInfo *font, float size, float x, float y, const char *pText, const float transform[2][2] = NULL);

#endif