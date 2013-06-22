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

#ifndef f_OPTDLG_H
#define f_OPTDLG_H

#include <windows.h>

#ifdef OPTDLG_STATICS
#define EXTERN
#define INIT(x) =x
#else
#define EXTERN extern
#define INIT(x)
#endif

typedef struct {
	long samp_frac;
	BOOL is_stereo, is_16bit, converting;
	char bytesPerSample;
} AudioInterleaveOptions;

EXTERN AudioInterleaveOptions audioInt;

/////////////////

void ActivateDubDialog(HINSTANCE hInst, LPCTSTR lpResource, HWND hDlg, DLGPROC dlgProc);
BOOL APIENTRY AudioConversionDlgProc	( HWND hDlg, UINT message, UINT wParam, LONG lParam);
BOOL APIENTRY AudioInterleaveDlgProc	( HWND hDlg, UINT message, UINT wParam, LONG lParam);
BOOL APIENTRY VideoDepthDlgProc			( HWND hDlg, UINT message, UINT wParam, LONG lParam);
BOOL APIENTRY PerformanceOptionsDlgProc	( HWND hDlg, UINT message, UINT wParam, LONG lParam);
BOOL APIENTRY DynamicCompileOptionsDlgProc( HWND hDlg, UINT message, UINT wParam, LONG lParam);
BOOL APIENTRY VideoDecimationDlgProc	( HWND hDlg, UINT message, UINT wParam, LONG lParam);
BOOL APIENTRY VideoClippingDlgProc		( HWND hDlg, UINT message, UINT wParam, LONG lParam);
BOOL CALLBACK VideoJumpDlgProc			(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam);

#undef EXTERN
#undef INIT

#endif