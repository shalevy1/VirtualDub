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

#include <stdio.h>
#include <crtdbg.h>
#include <math.h>

#include <windows.h>
#include <commctrl.h>

#include "ScriptInterpreter.h"
#include "ScriptValue.h"
#include "ScriptError.h"
#include "LevelControl.h"

#include "resource.h"
#include "filter.h"
#include "gui.h"

/////////////////////////////////////////////////////////////////////

extern HINSTANCE g_hInst;

typedef struct LevelsFilterData {
	unsigned char xtblmono[256];

	int			iInputLo, iInputHi;
	int			iOutputLo, iOutputHi;
	double		rHalfPt, rGammaCorr;

	IFilterPreview *ifp;
	RECT		rHisto;
	long *		pHisto;
	long		lHistoMax;
	bool		fInhibitUpdate;
} LevelsFilterData;

/////////////////////////////////////////////////////////////////////

static int levels_init(FilterActivation *fa, const FilterFunctions *ff) {
	LevelsFilterData *mfd = (LevelsFilterData *)fa->filter_data;

	mfd->iInputLo = 0x0000;
	mfd->rHalfPt = 0.5;
	mfd->rGammaCorr = 1.0;
	mfd->iInputHi = 0xFFFF;
	mfd->iOutputHi = 0xFFFF;

	return 0;
}

static int levels_run(const FilterActivation *fa, const FilterFunctions *ff) {
	const LevelsFilterData *mfd = (LevelsFilterData *)fa->filter_data;

	fa->dst.BitBltXlat1(0, 0, &fa->src, 0, 0, -1, -1, mfd->xtblmono);

	return 0;
}

static long levels_param(FilterActivation *fa, const FilterFunctions *ff) {
	LevelsFilterData *mfd = (LevelsFilterData *)fa->filter_data;

	fa->dst.offset = fa->src.offset;

	return 0;
}

static void levelsButtonCallback(bool fNewState, void *pvData) {
	HWND hdlg = (HWND)pvData;

	EnableWindow(GetDlgItem(hdlg, IDC_SAMPLE), fNewState);
	EnableWindow(GetDlgItem(hdlg, IDC_SAMPLE_MULTIPLE), fNewState);
}

static void levelsRedoTables(LevelsFilterData *mfd) {
	int i;
	const long		y_base		= mfd->iOutputLo,
					y_range		= mfd->iOutputHi - mfd->iOutputLo;
	const double	x_lo		= mfd->iInputLo / (double)0xffff,
					x_hi		= mfd->iInputHi / (double)0xffff;

	for(i=0; i<256; i++) {
		double y, x;

		x = i / 255.0;

		if (x < x_lo)
			mfd->xtblmono[i] = mfd->iOutputLo >> 8;
		else if (x > x_hi)
			mfd->xtblmono[i] = mfd->iOutputHi >> 8;
		else {
			y = pow((x - x_lo) / (x_hi - x_lo), 1.0/mfd->rGammaCorr);

			mfd->xtblmono[i] = (unsigned char)((int)(0.5 + y_base + y_range * y) >> 8);
		}
	}
}

static void levelsSampleCallback(VFBitmap *src, long pos, long cnt, void *pv) {
	LevelsFilterData *mfd = (LevelsFilterData *)pv;
	long *pHisto = mfd->pHisto;

	src->Histogram(0, 0, -1, -1, pHisto, VFBitmap::HISTO_GRAY);
}

static void levelsSampleDisplay(LevelsFilterData *mfd, HWND hdlg) {
	int i;
	long *pHisto = mfd->pHisto;

	if (mfd->lHistoMax < 0)
		ShowWindow(GetDlgItem(hdlg, IDC_HISTOGRAM), SW_HIDE);

	mfd->lHistoMax = pHisto[0];

	for(i=1; i<256; i++)
		if (pHisto[i] > mfd->lHistoMax)
			mfd->lHistoMax = pHisto[i];

	InvalidateRect(hdlg, &mfd->rHisto, FALSE);
	UpdateWindow(hdlg);
}

static BOOL APIENTRY levelsDlgProc( HWND hDlg, UINT message, UINT wParam, LONG lParam) {
	LevelsFilterData *mfd = (struct LevelsFilterData *)GetWindowLong(hDlg, DWL_USER);
	char buf[32];

    switch (message)
    {
        case WM_INITDIALOG:
			{
				HWND hwndItem;

				mfd = (struct LevelsFilterData *)lParam;
				SetWindowLong(hDlg, DWL_USER, lParam);

				GetWindowRect(GetDlgItem(hDlg, IDC_HISTOGRAM), &mfd->rHisto);
				ScreenToClient(hDlg, (POINT *)&mfd->rHisto + 0);
				ScreenToClient(hDlg, (POINT *)&mfd->rHisto + 1);

				mfd->fInhibitUpdate = false;

				hwndItem = GetDlgItem(hDlg, IDC_INPUT_LEVELS);

				SendMessage(hwndItem, VLCM_SETTABCOUNT, FALSE, 3);
				SendMessage(hwndItem, VLCM_SETTABCOLOR, MAKELONG(0, FALSE), 0x000000);
				SendMessage(hwndItem, VLCM_SETTABCOLOR, MAKELONG(1, FALSE), 0x808080);
				SendMessage(hwndItem, VLCM_SETTABCOLOR, MAKELONG(2, FALSE), 0xFFFFFF);
				SendMessage(hwndItem, VLCM_MOVETABPOS, MAKELONG(0, FALSE), mfd->iInputLo);
				SendMessage(hwndItem, VLCM_MOVETABPOS, MAKELONG(1, FALSE), (int)(mfd->iInputLo + (mfd->iInputHi-mfd->iInputLo)*mfd->rHalfPt));
				SendMessage(hwndItem, VLCM_MOVETABPOS, MAKELONG(2,  TRUE), mfd->iInputHi);
				SendMessage(hwndItem, VLCM_SETGRADIENT, 0x000000, 0xFFFFFF);

				hwndItem = GetDlgItem(hDlg, IDC_OUTPUT_LEVELS);

				SendMessage(hwndItem, VLCM_SETTABCOUNT, FALSE, 2);
				SendMessage(hwndItem, VLCM_SETTABCOLOR, MAKELONG(0, FALSE), 0x000000);
				SendMessage(hwndItem, VLCM_SETTABCOLOR, MAKELONG(1, FALSE), 0xFFFFFF);
				SendMessage(hwndItem, VLCM_MOVETABPOS, MAKELONG(0, FALSE), mfd->iOutputLo);
				SendMessage(hwndItem, VLCM_MOVETABPOS, MAKELONG(1,  TRUE), mfd->iOutputHi);
				SendMessage(hwndItem, VLCM_SETGRADIENT, 0x000000, 0xFFFFFF);

				mfd->ifp->SetButtonCallback(levelsButtonCallback, (void *)hDlg);
				mfd->ifp->SetSampleCallback(levelsSampleCallback, (void *)mfd);
				mfd->ifp->InitButton(GetDlgItem(hDlg, IDC_PREVIEW));

			}
            return (TRUE);

        case WM_COMMAND:
			if (mfd->fInhibitUpdate)
				return TRUE;

			switch(LOWORD(wParam)) {
            case IDOK:
				mfd->ifp->Close();
				EndDialog(hDlg, 0);
				return TRUE;

			case IDCANCEL:
				mfd->ifp->Close();
                EndDialog(hDlg, 1);
                return TRUE;

			case IDC_PREVIEW:
				mfd->ifp->Toggle(hDlg);
				return TRUE;

			case IDC_SAMPLE:
				memset(mfd->pHisto, 0, sizeof(mfd->pHisto[0])*256);
				mfd->ifp->SampleCurrentFrame();
				levelsSampleDisplay(mfd, hDlg);
				return TRUE;

			case IDC_SAMPLE_MULTIPLE:
				memset(mfd->pHisto, 0, sizeof(mfd->pHisto[0])*256);
				mfd->ifp->SampleFrames();
				levelsSampleDisplay(mfd, hDlg);
				return TRUE;

			case IDC_INPUTGAMMA:
				mfd->fInhibitUpdate = true;
				if (HIWORD(wParam) == EN_CHANGE) {
					double rv;

					if (GetWindowText((HWND)lParam, buf, sizeof buf))
						if (1 == sscanf(buf, "%lg", &rv)) {
							// pt^(1/rv) = 0.5
							// pt = 0.5^-(1/rv)

							if (rv < 0.01)
								rv = 0.01;
							else if (rv > 10.0)
								rv = 10.0;

							mfd->rGammaCorr = rv;
							mfd->rHalfPt = pow(0.5, rv);

							_RPT4(0, "%g %g %4X %4X\n", mfd->rHalfPt, mfd->rGammaCorr, mfd->iInputLo, mfd->iInputHi);

							SendDlgItemMessage(hDlg, IDC_INPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(1, TRUE),
									(int)(0.5 + mfd->iInputLo + (mfd->iInputHi - mfd->iInputLo)*mfd->rHalfPt));

							levelsRedoTables(mfd);
							mfd->ifp->RedoFrame();
						}
				} else if (HIWORD(wParam) == EN_KILLFOCUS) {
					sprintf(buf, "%.3f", mfd->rGammaCorr);
					SetWindowText((HWND)lParam, buf);
				}
				mfd->fInhibitUpdate = false;
				return TRUE;

			case IDC_INPUTLO:
				mfd->fInhibitUpdate = true;
				if (HIWORD(wParam) == EN_CHANGE) {
					BOOL f;
					int v;

					v = GetDlgItemInt(hDlg, IDC_INPUTLO, &f, FALSE) * 0x0101;

					if (v<0)
						v=0;
					else if (v>mfd->iInputHi)
						v = mfd->iInputHi;

					mfd->iInputLo = v;

					SendDlgItemMessage(hDlg, IDC_INPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(0, TRUE), v);
					SendDlgItemMessage(hDlg, IDC_INPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(1, TRUE), (int)floor(0.5 + mfd->iInputLo + (mfd->iInputHi-mfd->iInputLo)*mfd->rHalfPt));
					levelsRedoTables(mfd);
					mfd->ifp->RedoFrame();
				} else if (HIWORD(wParam) == EN_KILLFOCUS)
					SetDlgItemInt(hDlg, IDC_INPUTLO, mfd->iInputLo>>8, FALSE);

				mfd->fInhibitUpdate = false;
				return TRUE;
			case IDC_INPUTHI:
				mfd->fInhibitUpdate = true;
				if (HIWORD(wParam) == EN_CHANGE) {
					BOOL f;
					int v;

					v = GetDlgItemInt(hDlg, IDC_INPUTHI, &f, FALSE)*0x0101;

					if (v<mfd->iInputLo)
						v=mfd->iInputLo;
					else if (v>0xffff)
						v = 0xffff;

					mfd->iInputHi = v;

					SendDlgItemMessage(hDlg, IDC_INPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(2, TRUE), v);
					SendDlgItemMessage(hDlg, IDC_INPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(1, TRUE), (int)floor(0.5 + mfd->iInputLo + (mfd->iInputHi-mfd->iInputLo)*mfd->rHalfPt));
					levelsRedoTables(mfd);
					mfd->ifp->RedoFrame();
				} else if (HIWORD(wParam) == EN_KILLFOCUS)
					SetDlgItemInt(hDlg, IDC_INPUTHI, mfd->iInputHi>>8, FALSE);

				mfd->fInhibitUpdate = false;
				return TRUE;
			case IDC_OUTPUTLO:
				mfd->fInhibitUpdate = true;
				if (HIWORD(wParam) == EN_CHANGE) {
					BOOL f;
					int v;

					v = GetDlgItemInt(hDlg, IDC_OUTPUTLO, &f, FALSE)*0x0101;

					if (v<0)
						v=0;
					else if (v>mfd->iOutputHi)
						v = mfd->iOutputHi;

					mfd->iOutputLo = v;

					SendDlgItemMessage(hDlg, IDC_OUTPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(0, TRUE), v);

					levelsRedoTables(mfd);
					mfd->ifp->RedoFrame();

				} else if (HIWORD(wParam) == EN_KILLFOCUS)
					SetDlgItemInt(hDlg, IDC_OUTPUTLO, mfd->iOutputLo>>8, FALSE);

				mfd->fInhibitUpdate = false;
				return TRUE;
			case IDC_OUTPUTHI:
				mfd->fInhibitUpdate = true;
				if (HIWORD(wParam) == EN_CHANGE) {
					BOOL f;
					int v;

					v = GetDlgItemInt(hDlg, IDC_OUTPUTHI, &f, FALSE)*0x0101;

					if (v<mfd->iOutputLo)
						v=mfd->iOutputLo;
					else if (v>0xffff)
						v = 0xffff;

					mfd->iOutputHi = v;

					SendDlgItemMessage(hDlg, IDC_OUTPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(1, TRUE), v);

					levelsRedoTables(mfd);
					mfd->ifp->RedoFrame();
				} else if (HIWORD(wParam) == EN_KILLFOCUS)
					SetDlgItemInt(hDlg, IDC_OUTPUTHI, mfd->iOutputHi>>8, FALSE);

				mfd->fInhibitUpdate = false;
				return TRUE;
			}
			break;

		case WM_PAINT:
			if (mfd->lHistoMax < 0)
				return FALSE;
			{
				HDC hdc;
				PAINTSTRUCT ps;
				RECT rPaint, rClip;
				int i;

				hdc = BeginPaint(hDlg, &ps);

				i = GetClipBox(hdc, &rClip);

				if (i==ERROR || i==NULLREGION || IntersectRect(&rPaint, &mfd->rHisto, &rClip)) {
					int x, xlo, xhi, w;
					long lMax = mfd->lHistoMax;

					w = mfd->rHisto.right - mfd->rHisto.left;

					if (i==NULLREGION) {
						xlo = 0;
						xhi = w;
					} else {
						xlo = rPaint.left - mfd->rHisto.left;
						xhi = rPaint.right - mfd->rHisto.left;
					}

					FillRect(hdc, &mfd->rHisto, (HBRUSH)GetStockObject(WHITE_BRUSH));

					for(x=xlo; x<xhi; x++) {
						int xp, yp, h;
						long y;

						i = (x * 0xFF00) / (w-1);

						if (i >= 0xFF00)
							y = mfd->pHisto[255];
						else
							y = (long)(((__int64)mfd->pHisto[(i>>8)+0] * (256 - (i&255)) + (__int64)mfd->pHisto[(i>>8)+1] * (i&255)) >> 8);

						xp = x+mfd->rHisto.left;
						yp = mfd->rHisto.bottom-1;
						h = MulDiv(y, mfd->rHisto.bottom-mfd->rHisto.top, lMax);

						if (h>0) {
							MoveToEx(hdc, x+mfd->rHisto.left, yp, NULL);
							LineTo(hdc, x+mfd->rHisto.left, yp - h);
						}
					}
				}

				EndPaint(hDlg, &ps);
			}
			break;

		case WM_NOTIFY:
			if (!mfd->fInhibitUpdate) {
				NMHDR *pnmh = (NMHDR *)lParam;
				NMVLTABCHANGE *pnmvltc = (NMVLTABCHANGE *)lParam;
				char buf[32];

				mfd->fInhibitUpdate = true;

				switch(pnmh->idFrom) {
				case IDC_INPUT_LEVELS:
					switch(pnmvltc->iTab) {
					case 0:
						mfd->iInputLo = pnmvltc->iNewPos;
						SetDlgItemInt(hDlg, IDC_INPUTLO, mfd->iInputLo>>8, FALSE);
						UpdateWindow(GetDlgItem(hDlg, IDC_INPUTLO));
						SendDlgItemMessage(hDlg, IDC_INPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(1, TRUE), (int)floor(0.5 + mfd->iInputLo + (mfd->iInputHi-mfd->iInputLo)*mfd->rHalfPt));
						break;
					case 1:
						if (mfd->iInputLo == mfd->iInputHi) {
							mfd->rHalfPt = 0.5;
							mfd->rGammaCorr = 1.0;
						} else {

							// compute halfpoint... if inputlo/hi range is even, drop down by 1/2 to allow for halfpoint

							if ((mfd->iInputLo + mfd->iInputHi) & 1)
								mfd->rHalfPt = ((pnmvltc->iNewPos<=(mfd->iInputLo+mfd->iInputHi)/2 ? +1 : -1) + 2*(pnmvltc->iNewPos  - mfd->iInputLo))
										/ (2.0*(mfd->iInputHi - mfd->iInputLo - 1));
							else
								mfd->rHalfPt = (pnmvltc->iNewPos - mfd->iInputLo) / (double)(mfd->iInputHi - mfd->iInputLo);

							// halfpt ^ (1/gc) = 0.5
							// 1/gc = log_halfpt(0.5)
							// 1/gc = log(0.5) / log(halfpt)
							// gc = log(halfpt) / log(0.5)

							// clamp gc to [0.01...10.0]

							if (mfd->rHalfPt > 0.9930925)
								mfd->rHalfPt = 0.9930925;
							else if (mfd->rHalfPt < 0.0009765625)
								mfd->rHalfPt = 0.0009765625;

							mfd->rGammaCorr = log(mfd->rHalfPt) / -0.693147180559945309417232121458177;	// log(0.5);
						}

						sprintf(buf, "%.3f", mfd->rGammaCorr);
						SetDlgItemText(hDlg, IDC_INPUTGAMMA, buf);
						UpdateWindow(GetDlgItem(hDlg, IDC_INPUTGAMMA));
						break;
					case 2:
						mfd->iInputHi = pnmvltc->iNewPos;
						SetDlgItemInt(hDlg, IDC_INPUTHI, mfd->iInputHi>>8, FALSE);
						UpdateWindow(GetDlgItem(hDlg, IDC_INPUTHI));
						SendDlgItemMessage(hDlg, IDC_INPUT_LEVELS, VLCM_SETTABPOS, MAKELONG(1, TRUE), (int)floor(0.5 + mfd->iInputLo + (mfd->iInputHi-mfd->iInputLo)*mfd->rHalfPt));
						break;
					}
					levelsRedoTables(mfd);
					mfd->ifp->RedoFrame();
					mfd->fInhibitUpdate = false;
					return TRUE;
				case IDC_OUTPUT_LEVELS:
					switch(pnmvltc->iTab) {
					case 0:
						mfd->iOutputLo = pnmvltc->iNewPos;
						SetDlgItemInt(hDlg, IDC_OUTPUTLO, (pnmvltc->iNewPos >> 8), FALSE);
						break;
					case 1:
						mfd->iOutputHi = pnmvltc->iNewPos;
						SetDlgItemInt(hDlg, IDC_OUTPUTHI, (pnmvltc->iNewPos >> 8), FALSE);
						break;
					}
					levelsRedoTables(mfd);
					mfd->ifp->RedoFrame();
					mfd->fInhibitUpdate = false;
					return TRUE;
				}
				mfd->fInhibitUpdate = false;
			}
			break;
    }
    return FALSE;
}

static int levels_config(FilterActivation *fa, const FilterFunctions *ff, HWND hWnd) {
	LevelsFilterData *mfd = (LevelsFilterData *)fa->filter_data;
	LevelsFilterData mfd2 = *mfd;
	int ret;
	long histo[256];

	mfd->ifp = fa->ifp;
	mfd->pHisto = histo;
	mfd->lHistoMax = -1;

	ret = DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_FILTER_LEVELS), hWnd, levelsDlgProc, (LONG)mfd);

	if (ret)
		*mfd = mfd2;

	return ret;
}

///////////////////////////////////////////

static int levels_start(FilterActivation *fa, const FilterFunctions *ff) {
	LevelsFilterData *mfd = (LevelsFilterData *)fa->filter_data;

	levelsRedoTables(mfd);

	return 0;
}

static void levels_string(const FilterActivation *fa, const FilterFunctions *ff, char *buf) {
	LevelsFilterData *mfd = (LevelsFilterData *)fa->filter_data;

	sprintf(buf, " ( [%4.2f-%4.2f] > %.2f > [%4.2f-%4.2f] )",
			mfd->iInputLo/(double)0xffff,
			mfd->iInputHi/(double)0xffff,
			mfd->rGammaCorr,
			mfd->iOutputLo/(double)0xffff,
			mfd->iOutputHi/(double)0xffff
			);
}

static void levels_script_config(IScriptInterpreter *isi, void *lpVoid, CScriptValue *argv, int argc) {
	FilterActivation *fa = (FilterActivation *)lpVoid;
	LevelsFilterData *mfd = (LevelsFilterData *)fa->filter_data;

	mfd->iInputLo	= argv[0].asInt();
	mfd->iInputHi	= argv[1].asInt();
	mfd->rGammaCorr	= argv[2].asInt() / 16777216.0;
	mfd->rHalfPt	= pow(0.5, mfd->rGammaCorr);
	mfd->iOutputLo	= argv[3].asInt();
	mfd->iOutputHi	= argv[4].asInt();
}

static ScriptFunctionDef levels_func_defs[]={
	{ (ScriptFunctionPtr)levels_script_config, "Config", "0iiiii" },
	{ NULL },
};

static CScriptObject levels_obj={
	NULL, levels_func_defs
};

static bool levels_script_line(FilterActivation *fa, const FilterFunctions *ff, char *buf, int buflen) {
	LevelsFilterData *mfd = (LevelsFilterData *)fa->filter_data;

	_snprintf(buf, buflen, "Config(0x%04X,0x%04X,0x%08lX,0x%04X,0x%04X)"
				,mfd->iInputLo
				,mfd->iInputHi
				,(long)(0.5 + mfd->rGammaCorr * 16777216.0)
				,mfd->iOutputLo
				,mfd->iOutputHi
				);

	return true;
}

FilterDefinition filterDef_levels={
	0,0,NULL,
	"levels",
	"Applies a levels or levels-correction transform to the image."
			"\n\n[Assembly optimized]"
		,
	NULL,NULL,
	sizeof(LevelsFilterData),
	levels_init,NULL,
	levels_run,
	levels_param,
	levels_config,
	levels_string,
	levels_start,
	NULL,					// end

	&levels_obj,
	levels_script_line,
};