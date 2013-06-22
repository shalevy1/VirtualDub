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

#ifndef f_AVIOUTPUT_IMAGES_H
#define f_AVIOUTPUT_IMAGES_H

#include <windows.h>
#include <vfw.h>

class VideoSource;

class AVIOutputImages : public AVIOutput {
private:
	char szFormat[MAX_PATH];
	int iDigits;

public:
	AVIOutputImages(char *szFormat, int iDigits);
	~AVIOutputImages();

	BOOL initOutputStreams();
	BOOL init(const char *szFile, LONG xSize, LONG ySize, BOOL videoIn, BOOL audioIn, LONG bufferSize, BOOL is_interleaved);
	BOOL finalize();
	BOOL isPreview();

	void writeIndexedChunk(FOURCC ckid, LONG dwIndexFlags, LPVOID lpBuffer, LONG cbBuffer);
};

#endif