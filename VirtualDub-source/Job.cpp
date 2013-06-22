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

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include "resource.h"

#include "List.h"
#include "Error.h"
#include "InputFile.h"

#include "gui.h"
#include "job.h"
#include "command.h"
#include "dub.h"
#include "script.h"

///////////////////////////////////////////////////////////////////////////

extern HWND g_hWnd;
extern HINSTANCE g_hInst;
extern FilterFunctions g_filterFuncs;
extern char g_szInputAVIFile[];
extern char g_szInputWAVFile[];
extern char g_szInputAVIFileTitle[];
extern InputFileOptions *g_pInputOpts;

HWND g_hwndJobs;

bool g_fJobMode;
bool g_fJobAborted;

///////////////////////////////////////////////////////////////////////////

static BOOL CALLBACK JobCtlDlgProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam);

///////////////////////////////////////////////////////////////////////////

static char *strCify(const char *s) {
	static char buf[512];
	char c,*t = buf;

	while(c=*s++) {
		if (c<0x20 || c>=0x7f)
			t += sprintf(t, "\\x%02x", c);
		else {
			if (c=='"' || c=='\\')
				*t++ = '\\';
			*t++ = c;
		}
	}
	*t=0;

	return buf;
}

///////////////////////////////////////////////////////////////////////////

class JobScriptOutputBlock : public ListNode {
public:
	long size;
	long ptr;
	char data[];
};

class JobScriptOutput {
private:
	List listScript;
	JobScriptOutputBlock *jsob;
	long total;
public:
	JobScriptOutput();
	~JobScriptOutput();

	void clear();
	void write(const char *s, long l);
	void adds(const char *s);
	void addf(const char *fmt, ...);
	char *getscript();
};

///////

JobScriptOutput::JobScriptOutput() {
	clear();
}

JobScriptOutput::~JobScriptOutput() {
	clear();
}

void JobScriptOutput::clear() {
	JobScriptOutputBlock *jsob;

	while(jsob = (JobScriptOutputBlock *)listScript.RemoveHead())
		delete jsob;

	total = 0;
	this->jsob = NULL;
}

void JobScriptOutput::write(const char *s, long l) {
	long to_copy;

	total += l;

	while(l) {
		if (jsob && jsob->ptr<jsob->size) {
			to_copy = jsob->size-jsob->ptr;
			if (to_copy > l) to_copy = l;

			memcpy(jsob->data + jsob->ptr, s, to_copy);
			jsob->ptr += to_copy;
			l -= to_copy;
		} else {
			jsob = (JobScriptOutputBlock *)malloc(16384);

			if (!jsob) throw MyMemoryError();

			jsob->size = 16384 - sizeof(JobScriptOutputBlock);
			jsob->ptr = 0;
			listScript.AddTail(jsob);
		}
	}
}

void JobScriptOutput::adds(const char *s) {
	long l = strlen(s);

	write(s, l);
	write("\n",1);
//	_RPT1(0,">%s\n",s);
}

void JobScriptOutput::addf(const char *fmt, ...) {
	char buf[512];
	va_list val;
	long l;

	va_start(val, fmt);
	_vsnprintf(buf, sizeof buf, fmt, val);
	va_end(val);
	buf[sizeof buf-1]=0;

	l = strlen(buf);
	adds(buf);
}

char *JobScriptOutput::getscript() {
	char *mem = (char *)malloc(total+1), *t=mem;
	JobScriptOutputBlock *jsobptr;
	if (!mem) throw MyMemoryError();

	jsobptr = (JobScriptOutputBlock *)listScript.head.prev;

	while(jsobptr->prev) {
		memcpy(t, jsobptr->data, jsobptr->ptr);
		t += jsobptr->ptr;

		jsobptr = (JobScriptOutputBlock *)jsobptr->prev;
	}
	*t = 0;

	return mem;
}

///////////////////////////////////////////////////////////////////////////

class VDJob : public ListNode {
private:
	static List job_list;
	static long job_count;
	static bool fRunInProgress;
	static bool fRunAllStop;
	static bool fModified;

public:
	static long job_number;

	enum {
		WAITING		= 0,
		INPROGRESS	= 1,
		DONE		= 2,
		POSTPONED	= 3,
		ABORTED		= 4,
		ERR			= 5,
	};

	char szName[64];
	char szInputFile[MAX_PATH];
	char szOutputFile[MAX_PATH];
	char *szInputFileTitle;
	char *szOutputFileTitle;
	char szError[256];
	int iState;
	SYSTEMTIME stStart, stEnd;
	char *script;

	/////

	VDJob();
	~VDJob();

	void Add(bool force_no_update = false);
	void Delete(bool force_no_update = false);
	void Refresh();


	void Run();


	static VDJob *ListGet(int index);
	static int ListFind(VDJob *vdj_find);
	static long ListSize();
	static void ListClear(bool force_no_update = false);
	static void ListLoad(char *lpszName = NULL);

	static bool IsModified() {
		return fModified;
	}

	static void SetModified();

	static void Flush(char *lpfn =NULL);
	static void RunAll();
	static void RunAllStop();

	static bool IsRunInProgress() {
		return fRunInProgress;
	}
};

List VDJob::job_list;
long VDJob::job_count;
long VDJob::job_number=1;
bool VDJob::fModified = false;
bool VDJob::fRunInProgress = false;
bool VDJob::fRunAllStop;

VDJob::VDJob() {
	szName[0]=0;
	szInputFile[0]=0;
	szOutputFile[0]=0;
	iState = VDJob::WAITING;
	stStart.wYear = stEnd.wYear = 0;
	script = NULL;
}

VDJob::~VDJob() {
	delete script;
}

void VDJob::Add(bool force_no_update) {
	char c, *s;

	s = szInputFileTitle = szInputFile;
	while(c=*s++) if (c=='\\' || c==':') szInputFileTitle=s;
	
	s = szOutputFileTitle = szOutputFile;
	while(c=*s++) if (c=='\\' || c==':') szOutputFileTitle=s;

	job_list.AddHead(this);
	++job_count;

	if (g_hwndJobs) {
		LVITEM li;

		li.mask		= LVIF_TEXT;
		li.iSubItem	= 0;
		li.iItem	= job_count-1;
		li.pszText	= LPSTR_TEXTCALLBACK;

		ListView_InsertItem(GetDlgItem(g_hwndJobs, IDC_JOBS), &li);
	}

	if (!force_no_update) SetModified();
}

void VDJob::Delete(bool force_no_update) {
	int index = ListFind(this);

	if (index>=0 && g_hwndJobs)
		ListView_DeleteItem(GetDlgItem(g_hwndJobs, IDC_JOBS), index);

	ListNode::Remove();
	--job_count;
	
	if (!force_no_update) SetModified();
}

void VDJob::Refresh() {
	int index = ListFind(this);
//	bool fSelected;

	if (index>=0 && g_hwndJobs) {
		HWND hwndItem = GetDlgItem(g_hwndJobs, IDC_JOBS);

		ListView_Update(hwndItem, index);
	}
}

void VDJob::Run() {
	iState = INPROGRESS;
	GetLocalTime(&stStart);
	memset(&stEnd, 0, sizeof(SYSTEMTIME));
	Refresh();
	Flush();

	strcpy(g_szInputAVIFile, szInputFile);
	strcpy(g_szInputAVIFileTitle, szInputFileTitle);

	EnableWindow(GetDlgItem(g_hwndJobs, IDC_PROGRESS), TRUE);
	EnableWindow(GetDlgItem(g_hwndJobs, IDC_PERCENT), TRUE);

	try {
		g_fJobMode = true;
		g_fJobAborted = false;
		_CrtCheckMemory();
		RunScriptMemory(script);
		_CrtCheckMemory();
		g_fJobMode = false;
	} catch(MyError err) {
		iState = ERR;
		strcpy(szError, err.gets());
	}

	EnableWindow(GetDlgItem(g_hwndJobs, IDC_PROGRESS), FALSE);
	EnableWindow(GetDlgItem(g_hwndJobs, IDC_PERCENT), FALSE);

	if (iState == INPROGRESS) {
		if (g_fJobAborted)
			iState = ABORTED;
		else
			iState = DONE;
	}
	GetLocalTime(&stEnd);
	Refresh();
	Flush();
}

////////

VDJob *VDJob::ListGet(int index) {
	VDJob *vdj = (VDJob *)job_list.tail.next, *vdj_next;

	while((vdj_next = (VDJob *)vdj->next) && index--)
		vdj = vdj_next;

	if (!vdj_next) return NULL;

	return vdj;
}

int VDJob::ListFind(VDJob *vdj_find) {
	VDJob *vdj = (VDJob *)job_list.tail.next, *vdj_next;
	int index=0;

	while((vdj_next = (VDJob *)vdj->next) && vdj != vdj_find) {
		vdj = vdj_next;
		++index;
	}

	if (vdj == vdj_find) return index;

	return -1;
}

long VDJob::ListSize() {
	return job_count;
}

// VDJob::ListClear()
//
// Clears all jobs from the list.

void VDJob::ListClear(bool force_no_update) {
	VDJob *vdj;

	while((vdj = (VDJob *)job_list.tail.next)->next) {
		vdj->Delete(true);
		delete vdj;
	}

	if (!force_no_update) SetModified();
}

// VDJob::ListLoad()
//
// Loads the list from a file.

static char *findcmdline(char *s) {
	while(isspace(*s)) ++s;

	if (s[0] != '/' || s[1] != '/')
		return NULL;

	s+=2;

	while(isspace(*s)) ++s;
	if (*s++ != '$') return NULL;

	return s;
}

static void strgetarg(char *buf, long bufsiz, const char *s) {
	const char *t = s;
	long l;

	if (*t == '"') {
		s = ++t;
		while(*s && *s!='"') ++s;
	} else
		while(*s && !isspace(*s)) ++s;

	l = s-t;
	if (l > bufsiz-1)
		l = bufsiz-1;

	memcpy(buf, t, l);
	buf[l]=0;
}

void VDJob::ListLoad(char *lpszName) {
	FILE *f = NULL;
	char szName[MAX_PATH], szVDPath[MAX_PATH], *lpFilePart;
	char linebuf[1024];
	VDJob *job = NULL;

	// Try to create VirtualDub.jobs in the same directory as VirtualDub.

	if (!lpszName) {
		lpszName = szName;

		if (!GetModuleFileName(NULL, szVDPath, sizeof szVDPath))
			return;

		if (!GetFullPathName(szVDPath, sizeof szName, szName, &lpFilePart))
			return;

		strcpy(lpFilePart, "VirtualDub.jobs");
	}
	try {
		BOOL script_capture = false;
		JobScriptOutput jso;

		f = fopen(lpszName, "r");
		if (!f) return;

		ListClear(true);

		while(fgets(linebuf, sizeof linebuf, f)) {
			char *s;

			// kill the ending newline

			if (s = strchr(linebuf,'\n'))
				*s = 0;

			// scan for a command

			if (s = findcmdline(linebuf)) {
				char *t = s;

				while(isalpha(*t) || *t=='_') ++t;

				if (*t) *t++=0;
				while(isspace(*t)) ++t;

				if (!stricmp(s, "job")) {
					if (!(job = new VDJob)) throw "out of memory";
					job->szError[0]=0;

					strgetarg(job->szName, sizeof job->szName, t);

				} else if (!stricmp(s, "input")) {

					strgetarg(job->szInputFile, sizeof job->szInputFile, t);

				} else if (!stricmp(s, "output")) {

					strgetarg(job->szOutputFile, sizeof job->szOutputFile, t);

				} else if (!stricmp(s, "error")) {

					strgetarg(job->szError, sizeof job->szError, t);

				} else if (!stricmp(s, "state")) {

					job->iState = atoi(t);

					// Make sure "In Progress" states change to Aborted

					if (job->iState == INPROGRESS)
						job->iState = ABORTED;

				} else if (!stricmp(s, "start_time")) {
					FILETIME ft;

					if (2 != sscanf(t, "%08lx %08lx", &ft.dwHighDateTime, &ft.dwLowDateTime))
						throw "invalid start time";

					if (!ft.dwHighDateTime && !ft.dwLowDateTime)
						memset(&job->stStart, 0, sizeof(SYSTEMTIME));
					else
						FileTimeToSystemTime(&ft, &job->stStart);

				} else if (!stricmp(s, "end_time")) {
					FILETIME ft;

					if (2 != sscanf(t, "%08lx %08lx", &ft.dwHighDateTime, &ft.dwLowDateTime))
						throw "invalid start time";

					if (!ft.dwHighDateTime && !ft.dwLowDateTime)
						memset(&job->stEnd, 0, sizeof(SYSTEMTIME));
					else
						FileTimeToSystemTime(&ft, &job->stEnd);

				} else if (!stricmp(s, "script")) {

					script_capture = true;

				} else if (!stricmp(s, "endjob")) {
					if (script_capture) {
						job->script = jso.getscript();
						jso.clear();
						script_capture = false;
					}

					job->Add(true);
					job = NULL;;
				}
			} else if (script_capture) {
				// kill starting spaces

				s = linebuf;

				while(isspace(*s)) ++s;

				// don't add blank lines

				if (*s)
					jso.adds(s);
			}
		}

	} catch(int e) {
		_RPT0(0,"I/O error on job load\n");
		if (lpszName != szName) {
			if (f) fclose(f);
			throw MyError("Failure loading job list: %s.", strerror(e));
		}
	} catch(char *s) {
		_RPT0(0,s);
		if (lpszName != szName) {
			if (f) fclose(f);
			throw MyError(s);
		}
	}

	if (f) fclose(f);
	delete job;
}

void VDJob::SetModified() {
	fModified = true;

	if (!g_hwndJobs)
		Flush();
}

// VDJob::Flush()
//
// Flushes the job list out to disk.
//
// We store the job list in a file called VirtualDub.jobs.  It's actually a
// human-readable, human-editable Sylia script with extra comments to tell
// VirtualDub about each of the scripts.

void VDJob::Flush(char *lpszFileName) {
	FILE *f = NULL;
	char szName[MAX_PATH], szVDPath[MAX_PATH], *lpFilePart;

	_RPT0(0,"VDJob::Flush()\n");

	// Try to create VirtualDub.jobs in the same directory as VirtualDub.

	if (!lpszFileName) {
		if (!GetModuleFileName(NULL, szVDPath, sizeof szVDPath))
			return;

		if (!GetFullPathName(szVDPath, sizeof szName, szName, &lpFilePart))
			return;

		strcpy(lpFilePart, "VirtualDub.jobs");

		lpszFileName = szName;
	}

	try {
		VDJob *vdj, *vdj_next;

		f = fopen(lpszFileName, "w");
		if (!f) throw errno;

		if (fprintf(f,
				"// VirtualDub job list (Sylia script format)\n"
				"// This is a program generated file -- edit at your own risk.\n"
				"//\n"
				"// $numjobs %d\n"
				"//\n\n"
				,VDJob::ListSize()
				)<0)
			throw errno;

		vdj = (VDJob *)job_list.tail.next;

		while(vdj_next = (VDJob *)vdj->next) {
			FILETIME ft;
			char *s, *t, c;

			if (fprintf(f,"// $job \"%s\""			"\n", vdj->szName)<0) throw errno;
			if (fprintf(f,"// $input \"%s\""		"\n", vdj->szInputFile)<0) throw errno;
			if (fprintf(f,"// $output \"%s\""		"\n", vdj->szOutputFile)<0) throw errno;
			if (fprintf(f,"// $state %d"			"\n", vdj->iState)<0) throw errno;

			if (vdj->stStart.wYear) {
				SystemTimeToFileTime(&vdj->stStart, &ft);
				if (fprintf(f,"// $start_time %08lx %08lx"	"\n", ft.dwHighDateTime, ft.dwLowDateTime)<0) throw errno;
			} else
				if (fprintf(f,"// $start_time 0 0\n")<0) throw errno;

			if (vdj->stEnd.wYear) {
				SystemTimeToFileTime(&vdj->stEnd, &ft);
				if (fprintf(f,"// $end_time %08lx %08lx"	"\n", ft.dwHighDateTime, ft.dwLowDateTime)<0) throw errno;
			} else
				if (fprintf(f,"// $end_time 0 0\n")<0) throw errno;

			if (vdj->iState == ERR)
				if (fprintf(f,"// $error \"%s\"\n", vdj->szError)<0) throw errno;

			if (fprintf(f,"// $script\n\n")<0) throw errno;

			// Dump script

			s = vdj->script;

			while(*s) {
				t=s;

				while((c=*t) && c!='\r' && c!='\n')
					++t;

				if (t>s)
					if (1!=fwrite(s, t-s, 1, f)) throw errno;
				if (EOF==putc('\n', f)) throw errno;

				// handle CR, CR/LF, LF, and NUL terminators

				if (c == '\r') ++t;
				if (c == '\n') ++t;

				s=t;
			}

			// Next...

			if (fputs(
					"\n"
					"// $endjob\n"
					"//\n"
					"//--------------------------------------------------\n"
				,f)==EOF) throw errno;

			vdj = (VDJob *)vdj->next;
		}

		if (fprintf(f,"// $done\n")<0) throw errno;

		if (fflush(f)) throw errno;

		if (lpszFileName == szName) fModified = false;

	} catch(int) {
		_RPT0(0,"I/O error on job flush\n");
		if (lpszFileName) {
			if (f) fclose(f);
			throw MyError("Job list flush failed: %s.", strerror(errno));
		}
	}

	if (f) fclose(f);
}

void VDJob::RunAll() {
	VDJob *vdj = (VDJob *)job_list.tail.next;

	fRunInProgress	= true;
	fRunAllStop		= false;

	if (g_hwndJobs) {
		SetDlgItemText(g_hwndJobs, IDC_START, "Stop");
		EnableWindow(GetDlgItem(g_hwndJobs, IDC_ABORT), TRUE);
	}

	ShowWindow(g_hWnd, SW_MINIMIZE);

	while((VDJob *)vdj->next && !fRunAllStop) {
		if (vdj->iState == WAITING)
			vdj->Run();

		vdj = (VDJob *)vdj->next;
	}

//	ShowWindow(g_hWnd, SW_RESTORE);

	fRunInProgress = false;

	if (g_hwndJobs) {
		EnableWindow(GetDlgItem(g_hwndJobs, IDC_START), TRUE);
		SetDlgItemText(g_hwndJobs, IDC_START, "Start");
		EnableWindow(GetDlgItem(g_hwndJobs, IDC_ABORT), FALSE);
	}
}

void VDJob::RunAllStop() {
	fRunAllStop = true;
}

///////////////////////////////////////////////////////////////////////////

static BOOL CALLBACK JobErrorDlgProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam) {

	switch(uiMsg) {
	case WM_INITDIALOG:
		{
			VDJob *vdj = (VDJob *)lParam;
			char buf[128];

			wsprintf(buf, "VirtualDub - Job \"%s\"", vdj->szName);
			SetWindowText(hdlg, buf);

			SetDlgItemText(hdlg, IDC_ERROR, vdj->szError);
		}
		return TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
			EndDialog(hdlg, 0);
			return TRUE;
		}
		break;
	}

	return FALSE;
}

static void Job_GetDispInfo(NMLVDISPINFO *nldi) {
	VDJob *vdj = VDJob::ListGet(nldi->item.iItem);
	SYSTEMTIME *st = &vdj->stEnd;
	SYSTEMTIME ct;
	static const char *dow[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

	nldi->item.mask			= LVIF_TEXT;
	nldi->item.pszText[0]	= 0;

	switch(nldi->item.iSubItem) {
	case 0:
		nldi->item.pszText = vdj->szName;
		break;
	case 1:		// file in
		nldi->item.pszText = vdj->szInputFileTitle;
		break;
	case 2:		// file out
		nldi->item.pszText = vdj->szOutputFileTitle;
		break;
	case 3:		// time in
		st = &vdj->stStart;
	case 4:		// time out
		GetLocalTime(&ct);
		if (!st->wYear)
			nldi->item.pszText = "-";
		else if (ct.wYear != st->wYear
			|| ct.wMonth != st->wMonth
			|| ct.wDay != st->wDay) {

			_snprintf(nldi->item.pszText, nldi->item.cchTextMax, "%s %d %d:%02d%c"
						,dow[st->wDayOfWeek]
						,st->wDay
						,st->wHour==12||!st->wHour ? 12 : st->wHour%12
						,st->wMinute
						,st->wHour>=12 ? 'p' : 'a');
		} else {
			_snprintf(nldi->item.pszText, nldi->item.cchTextMax, "%d:%02d%c"
						,st->wHour==12||!st->wHour ? 12 : st->wHour%12
						,st->wMinute
						,st->wHour>=12 ? 'p' : 'a');
		}
		break;
	case 5:		// status
		switch(vdj->iState) {
		case VDJob::WAITING:	nldi->item.pszText = "Waiting"		; break;
		case VDJob::INPROGRESS:	nldi->item.pszText = "In progress"	; break;
		case VDJob::DONE:		nldi->item.pszText = "Done"			; break;
		case VDJob::POSTPONED:	nldi->item.pszText = "Postponed"	; break;
		case VDJob::ABORTED:	nldi->item.pszText = "Aborted"		; break;
		case VDJob::ERR:		nldi->item.pszText = "Error"		; break;
		}
		break;
	}
}


static void JobProcessDirectory(HWND hDlg) {
	char szSourceDir[MAX_PATH];
	char szDestDir[MAX_PATH];
	char *lpszFileName;
	BROWSEINFO bi;
	LPITEMIDLIST pidlBrowse;
	LPMALLOC pMalloc;
	bool fAbort = false;

	if (SUCCEEDED(SHGetMalloc(&pMalloc))) {
		if (lpszFileName = (char *)pMalloc->Alloc(MAX_PATH)) {
			bi.hwndOwner		= hDlg;
			bi.pidlRoot			= NULL;
			bi.pszDisplayName	= lpszFileName;
			bi.lpszTitle		= "Select source directory";
			bi.ulFlags			= BIF_RETURNONLYFSDIRS;
			bi.lpfn				= NULL;

			if (pidlBrowse = SHBrowseForFolder(&bi)) {
				if (SHGetPathFromIDList(pidlBrowse, lpszFileName))
					strcpy(szSourceDir, lpszFileName);
				else
					fAbort = true;

				pMalloc->Free(pidlBrowse);
			}

			bi.lpszTitle		= "Select destination directory";

			if (!fAbort && (pidlBrowse = SHBrowseForFolder(&bi))) {
				if (SHGetPathFromIDList(pidlBrowse, lpszFileName))
					strcpy(szDestDir, lpszFileName);

				pMalloc->Free(pidlBrowse);
			}
			pMalloc->Free(lpszFileName);
		}
	}

	if (fAbort) return;

	JobAddBatchDirectory(szSourceDir, szDestDir);

}

void Job_MenuHit(HWND hdlg, WPARAM wParam) {
	static char *fileFilters=
		"VirtualDub job list (*.jobs)\0"		"*.jobs\0"
		"Sylia script for VirtualDub (*.syl)\0"	"*.syl\0"
		"All files (*.*)\0"						"*.*\0";
	VDJob *vdj, *vdj_next;

	try {
		switch(LOWORD(wParam)) {

			case ID_FILE_LOADJOBLIST:
				{
					OPENFILENAME ofn;
					char szFile[MAX_PATH];

					///////////////

					szFile[0] = 0;

					ofn.lStructSize			= sizeof(OPENFILENAME);
					ofn.hwndOwner			= hdlg;
					ofn.lpstrFilter			= fileFilters;
					ofn.lpstrCustomFilter	= NULL;
					ofn.nFilterIndex		= 1;
					ofn.lpstrFile			= szFile;
					ofn.nMaxFile			= sizeof szFile;
					ofn.lpstrFileTitle		= NULL;
					ofn.nMaxFileTitle		= 0;
					ofn.lpstrInitialDir		= NULL;
					ofn.lpstrTitle			= "Load job list";
					ofn.Flags				= OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_ENABLESIZING;
					ofn.lpstrDefExt			= NULL;

					if (GetOpenFileName(&ofn))
						VDJob::ListLoad(szFile);
				}
				break;

			case ID_FILE_SAVEJOBLIST:
				{
					OPENFILENAME ofn;
					char szFile[MAX_PATH];

					///////////////

					szFile[0] = 0;

					ofn.lStructSize			= sizeof(OPENFILENAME);
					ofn.hwndOwner			= hdlg;
					ofn.lpstrFilter			= fileFilters;
					ofn.lpstrCustomFilter	= NULL;
					ofn.nFilterIndex		= 1;
					ofn.lpstrFile			= szFile;
					ofn.nMaxFile			= sizeof szFile;
					ofn.lpstrFileTitle		= NULL;
					ofn.nMaxFileTitle		= 0;
					ofn.lpstrInitialDir		= NULL;
					ofn.lpstrTitle			= "Save job list";
					ofn.Flags				= OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_ENABLESIZING;
					ofn.lpstrDefExt			= NULL;

					if (GetSaveFileName(&ofn))
						vdj->Flush(szFile);
				}
				break;

			case ID_EDIT_CLEARLIST:
				if (IDOK != MessageBox(hdlg, "Really clear job list?", "VirtualDub job system", MB_OKCANCEL | MB_ICONEXCLAMATION))
					break;

				VDJob::ListClear(false);
				break;

			case ID_EDIT_DELETEDONEJOBS:

				if (!(vdj = VDJob::ListGet(0)))
					break;

				while(vdj_next = (VDJob *)vdj->next) {
					if (vdj->iState == VDJob::DONE) {
						vdj->Delete();
						delete vdj;
					}

					vdj = vdj_next;
				}

				break;

			case ID_EDIT_FAILEDTOWAITING
				:

				if (!(vdj = VDJob::ListGet(0)))
					break;

				while(vdj_next = (VDJob *)vdj->next) {
					if (vdj->iState == VDJob::ABORTED || vdj->iState == VDJob::ERR) {
						vdj->iState = VDJob::WAITING;
						vdj->Refresh();
					}

					vdj = vdj_next;
				}
				break;

			case ID_EDIT_WAITINGTOPOSTPONED:

				if (!(vdj = VDJob::ListGet(0)))
					break;

				while(vdj_next = (VDJob *)vdj->next) {
					if (vdj->iState == VDJob::WAITING) {
						vdj->iState = VDJob::POSTPONED;
						vdj->Refresh();
					}

					vdj = vdj_next;
				}
				break;

			case ID_EDIT_POSTPONEDTOWAITING:

				if (!(vdj = VDJob::ListGet(0)))
					break;

				while(vdj_next = (VDJob *)vdj->next) {
					if (vdj->iState == VDJob::POSTPONED) {
						vdj->iState = VDJob::WAITING;
						vdj->Refresh();
					}

					vdj = vdj_next;
				}
				break;

			case ID_EDIT_DONETOWAITING:

				if (!(vdj = VDJob::ListGet(0)))
					break;

				while(vdj_next = (VDJob *)vdj->next) {
					if (vdj->iState == VDJob::DONE) {
						vdj->iState = VDJob::WAITING;
						vdj->Refresh();
					}

					vdj = vdj_next;
				}
				break;

			case ID_EDIT_PROCESSDIRECTORY:
				JobProcessDirectory(hdlg);
				break;
		}
	} catch(MyError e) {
		e.post(hdlg, "Job system error");
	}
}

static struct ReposItem jobCtlPosData[]={
	{ IDOK			, REPOS_MOVERIGHT },
	{ IDC_MOVE_UP	, REPOS_MOVERIGHT },
	{ IDC_MOVE_DOWN	, REPOS_MOVERIGHT },
	{ IDC_POSTPONE	, REPOS_MOVERIGHT },
	{ IDC_DELETE	, REPOS_MOVERIGHT },
	{ IDC_START		, REPOS_MOVERIGHT },
	{ IDC_ABORT		, REPOS_MOVERIGHT },
	{ IDC_JOBS		, REPOS_SIZERIGHT | REPOS_SIZEDOWN },
	{ IDC_CURRENTJOB, REPOS_MOVEDOWN },
	{ IDC_PROGRESS	, REPOS_MOVEDOWN | REPOS_SIZERIGHT },
	{ IDC_PERCENT	, REPOS_MOVEDOWN | REPOS_MOVERIGHT },
	{ 0 }
};

POINT jobCtlPos[11];

static BOOL CALLBACK JobCtlDlgProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam) {
	static char *szColumnNames[]={ "Name","Source","Dest","Start","End","Status" };
	static int iColumnWidths[]={ 100,75,75,50,50,100 };

	static UINT uiUpdateTimer;
	static int iUpdateVal;
	static bool fUpdateDisable;
	static RECT rInitial;

	VDJob *vdj, *vdj_p, *vdj_p2, *vdj_n, *vdj_n2;
	int index;
	HWND hwndItem;

	switch(uiMsg) {
	case WM_INITDIALOG:
		{
			HWND hwndItem;
			LV_COLUMN lvc;
			int i;

			GetWindowRect(hdlg, &rInitial);

			hwndItem = GetDlgItem(hdlg, IDC_JOBS);
			fUpdateDisable = false;

			ListView_SetExtendedListViewStyleEx(hwndItem, LVS_EX_FULLROWSELECT , LVS_EX_FULLROWSELECT);

			for (i=0; i<6; i++) {
				lvc.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
				lvc.fmt = LVCFMT_LEFT;
				lvc.cx = iColumnWidths[i];
				lvc.pszText = szColumnNames[i];

				ListView_InsertColumn(hwndItem, i, &lvc);
			}

			for(i=0; i<VDJob::ListSize(); i++) {
				LVITEM li;

				li.mask		= LVIF_TEXT;
				li.iSubItem	= 0;
				li.iItem	= i;
				li.pszText	= LPSTR_TEXTCALLBACK;

				ListView_InsertItem(hwndItem, &li);
			}

			if (g_dubber) {
				EnableWindow(GetDlgItem(hdlg, IDC_START), FALSE);
				EnableWindow(GetDlgItem(hdlg, IDC_ABORT), FALSE);
			} else if (VDJob::IsRunInProgress()) {
				SetDlgItemText(hdlg, IDC_START, "Stop");
			} else {
				EnableWindow(GetDlgItem(hdlg, IDC_ABORT), FALSE);
			}

			SendDlgItemMessage(hdlg, IDC_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 16384));

			if (g_dubber || VDJob::IsRunInProgress()) {
				EnableWindow(GetDlgItem(hdlg, IDC_PROGRESS), FALSE);
				EnableWindow(GetDlgItem(hdlg, IDC_PERCENT), FALSE);
			} else {
				EnableWindow(GetDlgItem(hdlg, IDC_PROGRESS), TRUE);
				EnableWindow(GetDlgItem(hdlg, IDC_PERCENT), TRUE);
			}

			guiReposInit(hdlg, jobCtlPosData, jobCtlPos);

			uiUpdateTimer = SetTimer(hdlg, 1, 5000, NULL);

			iUpdateVal = 0;
		}
		return FALSE;

	case WM_TIMER:

		// Wait for two intervals to flush.  That way, if the job list
		// is set modified right before a timer message, we don't flush
		// immediately.

		if (VDJob::IsModified()) {
			if (iUpdateVal) {
				VDJob::Flush();
				iUpdateVal = 0;
			} else
				++iUpdateVal;
		} else
			iUpdateVal = 0;

		return TRUE;

	case WM_NOTIFY:
		{
			NMHDR *nm = (NMHDR *)lParam;

			if (nm->idFrom == IDC_JOBS) {
				NMLVDISPINFO *nldi = (NMLVDISPINFO *)nm;
				NMLISTVIEW *nmlv;
				VDJob *vdj;
				bool fSelected;

				switch(nm->code) {
				case LVN_GETDISPINFO:
					Job_GetDispInfo(nldi);
					return TRUE;
				case LVN_ENDLABELEDIT:
					SetWindowLong(hdlg, DWL_MSGRESULT, TRUE);
					vdj = VDJob::ListGet(nldi->item.iItem);

					if (vdj && nldi->item.pszText) {
						strncpy(vdj->szName, nldi->item.pszText, sizeof vdj->szName);
						vdj->szName[sizeof vdj->szName-1] = 0;
					}
					return TRUE;
				case LVN_ITEMCHANGED:

					if (fUpdateDisable) return TRUE;

					nmlv = (NMLISTVIEW *)lParam;
					vdj = VDJob::ListGet(nmlv->iItem);

					_RPT3(0,"Item %d, subitem %d: new state is %s\n",nmlv->iItem,nmlv->iSubItem,nmlv->uNewState & LVIS_SELECTED ? "selected" : "unselected");
					fSelected = !!(nmlv->uNewState & LVIS_SELECTED);

					EnableWindow(GetDlgItem(hdlg, IDC_MOVE_UP), fSelected && nmlv->iItem>0);
					EnableWindow(GetDlgItem(hdlg, IDC_MOVE_DOWN), fSelected && nmlv->iItem<VDJob::ListSize()-1);
					EnableWindow(GetDlgItem(hdlg, IDC_DELETE), fSelected && (!vdj || vdj->iState != VDJob::INPROGRESS));
					EnableWindow(GetDlgItem(hdlg, IDC_POSTPONE), fSelected && (!vdj || vdj->iState != VDJob::INPROGRESS));
					return TRUE;

				case LVN_KEYDOWN:
					switch(((LPNMLVKEYDOWN)lParam)->wVKey) {
					case VK_DELETE:
						SendMessage(hdlg, WM_COMMAND, IDC_DELETE, (LPARAM)GetDlgItem(hdlg, IDC_DELETE));
					}
					return TRUE;

				case NM_DBLCLK:

					//	Previous state		Next state		Action
					//	--------------		----------		------
					//	Error				Waiting			Show error message
					//	Done				Waiting
					//	Postponed			Waiting
					//	Aborted				Waiting
					//	All others			Postponed

					index = ListView_GetNextItem(GetDlgItem(hdlg, IDC_JOBS), -1, LVNI_ALL | LVNI_SELECTED);
					if (index>=0) {
						vdj = VDJob::ListGet(index);

						switch(vdj->iState) {
						case VDJob::ERR:
							DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_JOBERROR), hdlg, JobErrorDlgProc, (LPARAM)vdj);
						case VDJob::DONE:
						case VDJob::ABORTED:
							vdj->iState = VDJob::WAITING;
							vdj->Refresh();
							VDJob::SetModified();
						case VDJob::INPROGRESS:
							break;
						default:
							SendMessage(hdlg, WM_COMMAND, MAKELONG(IDC_POSTPONE, BN_CLICKED), (LPARAM)GetDlgItem(hdlg, IDC_POSTPONE));
						}
					}

					return TRUE;
				}
			}
		}
		break;

	case WM_DESTROY:
		_RPT0(0,"Destroy caught -- flushing job list.\n");
		g_hwndJobs = NULL;
		VDJob::Flush();
		return TRUE;

	case WM_CLOSE:
		DestroyWindow(hdlg);
		return TRUE;

	case WM_COMMAND:
		if (!lParam) {
			// menu hit

			Job_MenuHit(hdlg, wParam);
			return TRUE;

		} else if (HIWORD(wParam) == BN_CLICKED) {
			hwndItem = GetDlgItem(hdlg, IDC_JOBS);

			index = ListView_GetNextItem(hwndItem, -1, LVNI_ALL | LVNI_SELECTED);
			if (index>=0)
				vdj = VDJob::ListGet(index);
			else
				vdj = NULL;

			switch(LOWORD(wParam)) {

			case IDOK:
				DestroyWindow(hdlg);
				return TRUE;

			case IDC_DELETE:
				if (vdj) {
					// Do not delete jobs that are in progress!

					if (vdj->iState != VDJob::INPROGRESS) {
						fUpdateDisable = true;
						vdj->Delete();
						delete vdj;
						VDJob::SetModified();
						fUpdateDisable = false;
						if (VDJob::ListSize() > 0)
							ListView_SetItemState(hwndItem, index==VDJob::ListSize() ? index-1 : index, LVIS_SELECTED, LVIS_SELECTED);
					}
				}

				return TRUE;

			case IDC_POSTPONE:
				if (vdj) {
					// Do not postpone jobs in progress

					if (vdj->iState != VDJob::INPROGRESS) {
						if (vdj->iState == VDJob::POSTPONED)
							vdj->iState = VDJob::WAITING;
						else
							vdj->iState = VDJob::POSTPONED;

						vdj->Refresh();

						VDJob::SetModified();
					}
				}

				return TRUE;

			case IDC_MOVE_UP:
				if (!vdj || index <= 0)
					return TRUE;

				vdj_n	= (VDJob *)vdj->next;
				vdj_p	= (VDJob *)vdj->prev;
				vdj_p2	= (VDJob *)vdj_p->prev;

				vdj_p2->next = vdj;		vdj->prev = vdj_p2;
				vdj->next = vdj_p;		vdj_p->prev = vdj;
				vdj_p->next = vdj_n;	vdj_n->prev = vdj_p;

				ListView_SetItemState(hwndItem, index  , 0, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_SetItemState(hwndItem, index-1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_RedrawItems(hwndItem, index-1, index);

				VDJob::SetModified();

				return TRUE;

			case IDC_MOVE_DOWN:
				if (!vdj || index >= VDJob::ListSize()-1)
					return TRUE;

				vdj_p	= (VDJob *)vdj->prev;
				vdj_n	= (VDJob *)vdj->next;
				vdj_n2	= (VDJob *)vdj_n->next;

				vdj_p->next = vdj_n;	vdj_n->prev = vdj_p;
				vdj->prev = vdj_n;		vdj_n->next = vdj;
				vdj_n2->prev = vdj;		vdj->next = vdj_n2;
				
				ListView_SetItemState(hwndItem, index  , 0, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_SetItemState(hwndItem, index+1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_RedrawItems(hwndItem, index, index+1);

				VDJob::SetModified();

				return TRUE;

			case IDC_START:
				if (VDJob::IsRunInProgress()) {
					VDJob::RunAllStop();
					EnableWindow((HWND)lParam, FALSE);
				} else
					VDJob::RunAll();
				return TRUE;

			case IDC_ABORT:
				if (VDJob::IsRunInProgress()) {
					VDJob::RunAllStop();
					EnableWindow(GetDlgItem(hdlg, IDC_START), FALSE);
					EnableWindow((HWND)lParam, FALSE);
					if (g_dubber) g_dubber->Abort();
				}

				return TRUE;

			}
		}
		break;

	case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;

			lpmmi->ptMinTrackSize.x = rInitial.right - rInitial.left;
			lpmmi->ptMinTrackSize.y = rInitial.bottom - rInitial.top;
		}
		return TRUE;

	case WM_SIZE:
		guiReposResize(hdlg, jobCtlPosData, jobCtlPos);
		return TRUE;

	}
	return FALSE;
}

///////////////////////////////////////////////////////////////////////////

void JobCreateScript(JobScriptOutput& output, const DubOptions *opt) {
	char *mem= NULL;
	char buf[1024];
	long l;

	switch(audioInputMode) {
	case AUDIOIN_WAVE:
		output.addf("VirtualDub.audio.SetSource(\"%s\");", strCify(g_szInputWAVFile));
		break;

	default:
		output.addf("VirtualDub.audio.SetSource(%d);", audioInputMode);
		break;
	
	}

	output.addf("VirtualDub.audio.SetMode(%d);", opt->audio.mode);

	output.addf("VirtualDub.audio.SetInterleave(%d,%d,%d,%d,%d);",
			opt->audio.enabled,
			opt->audio.preload,
			opt->audio.interval,
			opt->audio.is_ms,
			opt->audio.offset);

	output.addf("VirtualDub.audio.SetClipMode(%d,%d);",
			opt->audio.fStartAudio,
			opt->audio.fEndAudio);

	output.addf("VirtualDub.audio.SetConversion(%d,%d,%d,%d,%d);",
			opt->audio.new_rate,
			opt->audio.newPrecision,
			opt->audio.newChannels,
			opt->audio.integral_rate,
			opt->audio.fHighQuality);

	if (g_ACompressionFormat) {
		if (g_ACompressionFormat->cbSize) {
			mem = (char *)malloc(((g_ACompressionFormat->cbSize+2)/3)*4 + 1);
			if (!mem) throw MyMemoryError();

			membase64(mem, (char *)(g_ACompressionFormat+1), g_ACompressionFormat->cbSize);
			output.addf("VirtualDub.audio.SetCompression(%d,%d,%d,%d,%d,%d,%d,\"%s\");"
						,g_ACompressionFormat->wFormatTag
						,g_ACompressionFormat->nSamplesPerSec
						,g_ACompressionFormat->nChannels
						,g_ACompressionFormat->wBitsPerSample
						,g_ACompressionFormat->nAvgBytesPerSec
						,g_ACompressionFormat->nBlockAlign
						,g_ACompressionFormat->cbSize
						,mem
						);

			free(mem);
		} else
			output.addf("VirtualDub.audio.SetCompression(%d,%d,%d,%d,%d,%d);"
						,g_ACompressionFormat->wFormatTag
						,g_ACompressionFormat->nSamplesPerSec
						,g_ACompressionFormat->nChannels
						,g_ACompressionFormat->wBitsPerSample
						,g_ACompressionFormat->nAvgBytesPerSec
						,g_ACompressionFormat->nBlockAlign
						);
	} else
		output.addf("VirtualDub.audio.SetCompression();");

	output.addf("VirtualDub.video.SetDepth(%d,%d);",
			16+8*opt->video.inputDepth,
			16+8*opt->video.outputDepth);

	output.addf("VirtualDub.video.SetMode(%d);",
			opt->video.mode);

	output.addf("VirtualDub.video.SetFrameRate(%d,%d);",
			opt->video.frameRateNewMicroSecs,
			opt->video.frameRateDecimation);

	output.addf("VirtualDub.video.SetIVTC(%d,%d,%d,%d);",
			opt->video.fInvTelecine,
			opt->video.fIVTCMode,
			opt->video.nIVTCOffset,
			opt->video.fIVTCPolarity);

	output.addf("VirtualDub.video.SetRange(%d,%d);",
			opt->video.lStartOffsetMS,
			opt->video.lEndOffsetMS);

	if (g_Vcompression.dwFlags & ICMF_COMPVARS_VALID) {
		output.addf("VirtualDub.video.SetCompression(0x%08lx,%d,%d,%d);",
				g_Vcompression.fccHandler,
				g_Vcompression.lKey,
				g_Vcompression.lQ,
				g_Vcompression.lDataRate);

		l = ICGetStateSize(g_Vcompression.hic);

		if (l>0) {
			mem = (char *)malloc(l + ((l+2)/3)*4 + 1);
			if (!mem) throw MyMemoryError();

			if (ICGetState(g_Vcompression.hic, mem, l)<0) {
				free(mem);
//				throw MyError("Bad state data returned from compressor");

				// Fine then, be that way.  Stupid Pinnacle DV200 driver.
			}

			if (mem) {
				membase64(mem+l, mem, l);
				output.addf("VirtualDub.video.SetCompData(%d,\"%s\");", l, mem+l);
				free(mem);
			}
		}

	} else
		output.addf("VirtualDub.video.SetCompression();");

	output.addf("VirtualDub.video.filters.Clear();");

	// Add filters

	FilterInstance *fa = (FilterInstance *)g_listFA.tail.next, *fa_next;
	int iFilter = 0;

	while(fa_next = (FilterInstance *)fa->next) {
		output.addf("VirtualDub.video.filters.Add(\"%s\");", strCify(fa->filter->name));

		if (fa->x1 || fa->y1 || fa->x2 || fa->y2)
			output.addf("VirtualDub.video.filters.instance[%d].SetClipping(%d,%d,%d,%d);"
						,iFilter
						,fa->x1
						,fa->y1
						,fa->x2
						,fa->y2
						);

		if (fa->filter->fssProc && fa->filter->fssProc(fa, &g_filterFuncs, buf, sizeof buf))
			output.addf("VirtualDub.video.filters.instance[%d].%s;", iFilter, buf);

		++iFilter;
		fa = fa_next;
	}

	// Add subset information

	if (inputSubset) {
		FrameSubsetNode *pfsn;

		output.addf("VirtualDub.subset.Clear();");

		if (pfsn = inputSubset->getFirstFrame())
			do {
				output.addf("VirtualDub.subset.AddFrame(%ld,%ld);", pfsn->start, pfsn->len);
			} while(pfsn = inputSubset->getNextFrame(pfsn));
	} else
		output.addf("VirtualDub.subset.Delete();");


}

void JobAddConfiguration(const DubOptions *opt, const char *szFileInput, int iFileMode, const char *szFileOutput, bool fCompatibility, List2<InputFilenameNode> *pListAppended, long lSpillThreshold, long lSpillFrameThreshold) {
	VDJob *vdj = new VDJob;
	JobScriptOutput output;

	try {

		do {
			if (g_pInputOpts) {
				int l;
				char buf[256];

				l = g_pInputOpts->write(buf, (sizeof buf)/7*3);

				if (l) {
					membase64(buf+l, (char *)buf, l);
					output.addf("VirtualDub.Open(\"%s\",%d,0,\"%s\");",strCify(szFileInput), iFileMode, buf+l);
					break;
				}
			}

			output.addf("VirtualDub.Open(\"%s\",%d,0);",strCify(szFileInput), iFileMode);

		} while(false);

		if (pListAppended) {
			InputFilenameNode *ifn = pListAppended->AtHead(), *ifn_next;

			if (ifn = ifn->NextFromHead())
				while(ifn_next = ifn->NextFromHead()) {
					output.addf("VirtualDub.Append(\"%s\");", strCify(ifn->name));
					ifn = ifn_next;
				}
		}

		JobCreateScript(output, opt);

		// Add actual run option

		if (lSpillThreshold)
			output.addf("VirtualDub.SaveSegmentedAVI(\"%s\", %d, %d);", strCify(szFileOutput), lSpillThreshold, lSpillFrameThreshold);
		else
			output.addf("VirtualDub.Save%sAVI(\"%s\");", fCompatibility ? "Compatible" : "", strCify(szFileOutput));
		output.adds("VirtualDub.Close();");

		///////////////////

		strncpy(vdj->szInputFile, szFileInput, sizeof vdj->szInputFile);
		strncpy(vdj->szOutputFile, szFileOutput, sizeof vdj->szOutputFile);
		sprintf(vdj->szName, "Job %d", VDJob::job_number++);

		vdj->script = output.getscript();
		vdj->Add();
	} catch(...) {
		free(vdj);
		throw;
	}
}

void JobWriteConfiguration(FILE *f, DubOptions *opt) {
	JobScriptOutput output;
	char *scr;

	JobCreateScript(output, opt);

	scr = output.getscript();

	if (fputs(scr, f)<0 || fflush(f)) {
		free(scr);
		throw MyError("Can't write configuration: %s.", strerror(errno));
	}

	free(scr);
}

///////////////////////////////////////////////////////////////////////////

bool InitJobSystem() {
	VDJob::ListLoad();

	return true;
}

void DeinitJobSystem() {
	VDJob::ListClear(true);
}

void OpenJobWindow() {
	if (g_hwndJobs) return;

	g_hwndJobs = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_JOBCONTROL), NULL, JobCtlDlgProc);
}

void CloseJobWindow() {
	if (g_hwndJobs)
		DestroyWindow(g_hwndJobs);
}

void JobLockDubber() {
	if (g_hwndJobs) {
		EnableWindow(GetDlgItem(g_hwndJobs, IDC_START), FALSE);
		EnableWindow(GetDlgItem(g_hwndJobs, IDC_ABORT), FALSE);
	}
}

void JobUnlockDubber() {
	if (g_hwndJobs) {
		EnableWindow(GetDlgItem(g_hwndJobs, IDC_START), TRUE);
		EnableWindow(GetDlgItem(g_hwndJobs, IDC_ABORT), TRUE);
	}
}

void JobPositionCallback(LONG start, LONG cur, LONG end) {
	char buf[8];

	if (g_hwndJobs) {
		SendMessage(GetDlgItem(g_hwndJobs, IDC_PROGRESS), PBM_SETPOS, MulDiv(cur, 16384, end-start), 0);
		wsprintf(buf, "%d%%", MulDiv(cur, 100, end-start));
		SetDlgItemText(g_hwndJobs, IDC_PERCENT, buf);
	}
}

void JobClearList() {
	VDJob::ListClear();
}

void JobRunList() {
	VDJob::RunAll();
}

void JobAddBatchDirectory(const char *lpszSrc, const char *lpszDst) {
	// Scan source directory

	HANDLE				h;
	WIN32_FIND_DATA		wfd;
	char *s, *t;
	char szSourceDir[MAX_PATH], szDestDir[MAX_PATH];

	strcpy(szSourceDir, lpszSrc);
	strcpy(szDestDir, lpszDst);

	s = szSourceDir;
	t = szDestDir;

	if (*s) {

		// If the path string is just \ or starts with x: or ends in a slash
		// then don't append a slash

		while(*s) ++s;

		if ((s==szSourceDir || s[-1]!='\\') && (!isalpha(szSourceDir[0]) || szSourceDir[1]!=':' || szSourceDir[2]))
			*s++ = '\\';

	}
	
	if (*t) {

		// If the path string is just \ or starts with x: or ends in a slash
		// then don't append a slash

		while(*t) ++t;

		if ((t==szDestDir || t[-1]!='\\') && (!isalpha(szDestDir[0]) || szDestDir[1]!=':' || szDestDir[2]))
			*t++ = '\\';

	}

	strcpy(s,"*.*");

	h = FindFirstFile(szSourceDir,&wfd);

	if (INVALID_HANDLE_VALUE != h) {
		do {
			if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				char *t2, *dot = NULL;

				strcpy(s, wfd.cFileName);
				strcpy(t, wfd.cFileName);

				// Replace extension with .avi

				t2 = t;
				while(*t2) if (*t2++ == '.') dot = t2;

				if (dot)
					strcpy(dot, "avi");
				else
					strcpy(t2, ".avi");

				// Add job!

				JobAddConfiguration(&g_dubOpts, szSourceDir, 0, szDestDir, false, NULL, 0, 0);
			}
		} while(FindNextFile(h,&wfd));
		FindClose(h);
	}
}
