//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2001 Avery Lee
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

#include "stdafx.h"

#include <windows.h>
#include <vfw.h>
#include <vd2/system/Error.h>
#include <vd2/Dita/resources.h>

#include "gui.h"
#include "AudioSource.h"
#include "AVIReadHandler.h"

//////////////////////////////////////////////////////////////////////////////

extern HWND g_hWnd;		// TODO: Remove in 1.5.0

//////////////////////////////////////////////////////////////////////////////

namespace {
	enum { kVDST_AudioSource = 6 };

	enum {
		kVDM_TruncatedMP3FormatFixed,
	};
}

//////////////////////////////////////////////////////////////////////////////

AudioSourceWAV::AudioSourceWAV(const wchar_t *szFile, LONG inputBufferSize)
{
	MMIOINFO mmi;
	VDStringA filenameA(VDTextWToA(szFile));

	memset(&mmi,0,sizeof mmi);
	mmi.cchBuffer	= inputBufferSize;
	hmmioFile		= mmioOpen((char *)filenameA.c_str(), &mmi, MMIO_READ | MMIO_ALLOCBUF);

	if (!hmmioFile) {
		const char *pError = "An unknown error has occurred";
		switch(mmi.wErrorRet) {
		case MMIOERR_FILENOTFOUND:		pError = "File not found"; break;
		case MMIOERR_OUTOFMEMORY:		pError = "Out of memory"; break;
		case MMIOERR_ACCESSDENIED:		pError = "Access is denied"; break;
		case MMIOERR_CANNOTOPEN:		// fall through
		case MMIOERR_INVALIDFILE:		pError = "The file could not be opened"; break;
		case MMIOERR_NETWORKERROR:		pError = "A network error occurred while opening the file"; break;
		case MMIOERR_PATHNOTFOUND:		pError = "The file path does not exist"; break;
		case MMIOERR_SHARINGVIOLATION:	pError = "The file is currently in use"; break;
		case MMIOERR_TOOMANYOPENFILES:	pError = "Too many files are already open"; break;
		}
		throw MyError("Cannot open \"%s\": %s.", filenameA.c_str(), pError);
	}
}

AudioSourceWAV::~AudioSourceWAV() {
	mmioClose(hmmioFile, 0);
}

bool AudioSourceWAV::init() {
	chunkRIFF.fccType = mmioFOURCC('W','A','V','E');
	if (MMSYSERR_NOERROR != mmioDescend(hmmioFile, &chunkRIFF, NULL, MMIO_FINDRIFF))
		return FALSE;

	chunkDATA.ckid = mmioFOURCC('f','m','t',' ');
	if (MMSYSERR_NOERROR != mmioDescend(hmmioFile, &chunkDATA, &chunkRIFF, MMIO_FINDCHUNK))
		return FALSE;

	if (!allocFormat(chunkDATA.cksize)) return FALSE;
	if (chunkDATA.cksize != mmioRead(hmmioFile, (char *)getWaveFormat(), chunkDATA.cksize))
		return FALSE;

	if (MMSYSERR_NOERROR != mmioAscend(hmmioFile, &chunkDATA, 0))
		return FALSE;

	chunkDATA.ckid = mmioFOURCC('d','a','t','a');
	if (MMSYSERR_NOERROR != mmioDescend(hmmioFile, &chunkDATA, &chunkRIFF, MMIO_FINDCHUNK))
		return FALSE;

	bytesPerSample	= getWaveFormat()->nBlockAlign; //getWaveFormat()->nAvgBytesPerSec / getWaveFormat()->nSamplesPerSec;
	lSampleFirst	= 0;
	lSampleLast		= chunkDATA.cksize / bytesPerSample;
	lCurrentSample	= -1;

	streamInfo.fccType					= streamtypeAUDIO;
	streamInfo.fccHandler				= 0;
	streamInfo.dwFlags					= 0;
	streamInfo.wPriority				= 0;
	streamInfo.wLanguage				= 0;
	streamInfo.dwInitialFrames			= 0;
	streamInfo.dwScale					= bytesPerSample;
	streamInfo.dwRate					= getWaveFormat()->nAvgBytesPerSec;
	streamInfo.dwStart					= 0;
	streamInfo.dwLength					= chunkDATA.cksize / bytesPerSample;
	streamInfo.dwSuggestedBufferSize	= 0;
	streamInfo.dwQuality				= 0xffffffff;
	streamInfo.dwSampleSize				= bytesPerSample;

	return TRUE;
}

int AudioSourceWAV::_read(LONG lStart, LONG lCount, LPVOID buffer, LONG cbBuffer, LONG *lBytesRead, LONG *lSamplesRead) {
	LONG lBytes = lCount * bytesPerSample;

	if (lBytes > cbBuffer) {
		lBytes = cbBuffer - cbBuffer % bytesPerSample;
		lCount = lBytes / bytesPerSample;
	}
	
	if (buffer) {
		if (lStart != lCurrentSample)
			if (-1 == mmioSeek(hmmioFile, chunkDATA.dwDataOffset + bytesPerSample*lStart, SEEK_SET))
				return AVIERR_FILEREAD;

		if (lBytes != mmioRead(hmmioFile, (char *)buffer, lBytes))
			return AVIERR_FILEREAD;

		lCurrentSample = lStart + lCount;
	}

	*lSamplesRead = lCount;
	*lBytesRead = lBytes;

	return AVIERR_OK;
}

///////////////////////////

AudioSourceAVI::AudioSourceAVI(IAVIReadHandler *pAVI, bool bAutomated) {
	pAVIFile	= pAVI;
	pAVIStream	= NULL;
	bQuiet = bAutomated;	// ugh, this needs to go... V1.5.0.
}

AudioSourceAVI::~AudioSourceAVI() {
	if (pAVIStream)
		delete pAVIStream;
}

bool AudioSourceAVI::init() {
	LONG format_len;

	pAVIStream = pAVIFile->GetStream(streamtypeAUDIO, 0);
	if (!pAVIStream) return FALSE;

	if (pAVIStream->Info(&streamInfo, sizeof streamInfo))
		return FALSE;

	pAVIStream->FormatSize(0, &format_len);

	if (!allocFormat(format_len)) return FALSE;

	if (pAVIStream->ReadFormat(0, getFormat(), &format_len))
		return FALSE;

	lSampleFirst = pAVIStream->Start();
	lSampleLast = pAVIStream->End();

	// Check for illegal (truncated) MP3 format.
	const WAVEFORMATEX *pwfex = getWaveFormat();

	if (pwfex->wFormatTag == WAVE_FORMAT_MPEGLAYER3 && format_len < sizeof(MPEGLAYER3WAVEFORMAT)) {
		MPEGLAYER3WAVEFORMAT wf;

		wf.wfx				= *pwfex;
		wf.wfx.cbSize		= MPEGLAYER3_WFX_EXTRA_BYTES;

		wf.wID				= MPEGLAYER3_ID_MPEG;

		// Attempt to detect the padding mode and block size for the stream.

		double byterate = wf.wfx.nAvgBytesPerSec;
		double fAverageFrameSize = 1152.0 * byterate / wf.wfx.nSamplesPerSec;

		int estimated_bitrate = (int)floor(0.5 + byterate * (1.0/1000.0)) * 8;
		double fEstimatedFrameSizeISO = 144000.0 * estimated_bitrate / wf.wfx.nSamplesPerSec;

		if (wf.wfx.nSamplesPerSec < 32000) {	// MPEG-2?
			fAverageFrameSize *= 0.5;
			fEstimatedFrameSizeISO *= 0.5;
		}

		double fEstimatedFrameSizePaddingOff = floor(fEstimatedFrameSizeISO);
		double fEstimatedFrameSizePaddingOn = fEstimatedFrameSizePaddingOff + 1.0;
		double fErrorISO = fabs(fEstimatedFrameSizeISO - fAverageFrameSize);
		double fErrorPaddingOn = fabs(fEstimatedFrameSizePaddingOn - fAverageFrameSize);
		double fErrorPaddingOff = fabs(fEstimatedFrameSizePaddingOff - fAverageFrameSize);

		if (fErrorISO <= fErrorPaddingOn)				// iso < on
			if (fErrorISO <= fErrorPaddingOff)			// iso < on, off
				wf.fdwFlags			= MPEGLAYER3_FLAG_PADDING_ISO;
			else										// off < iso < on
				wf.fdwFlags			= MPEGLAYER3_FLAG_PADDING_OFF;
		else											// on < iso
			if (fErrorPaddingOn <= fErrorPaddingOff)	// on < iso, off
				wf.fdwFlags			= MPEGLAYER3_FLAG_PADDING_ON;
			else										// off < on < iso
				wf.fdwFlags			= MPEGLAYER3_FLAG_PADDING_OFF;

		wf.nBlockSize		= (int)floor(0.5 + fAverageFrameSize);	// This is just a guess.  The MP3 codec always turns padding off, so I don't know whether this should be rounded up or not.
		wf.nFramesPerBlock	= 1;
		wf.nCodecDelay		= 1393;									// This is the number of samples padded by the compressor.  1393 is the value typically written by the codec.

		if (!allocFormat(sizeof wf))
			return FALSE;

		memcpy(getFormat(), &wf, sizeof wf);

		const int bad_len = format_len;
		const int good_len = sizeof(MPEGLAYER3WAVEFORMAT);
		VDLogAppMessage(kVDLogWarning, kVDST_AudioSource, kVDM_TruncatedMP3FormatFixed, 2, &bad_len, &good_len);
	}

	if (!bQuiet) {
		double mean, stddev, maxdev;

		if (pAVIStream->getVBRInfo(mean, stddev, maxdev)) {
			guiMessageBoxF(g_hWnd, "VBR audio stream detected", MB_OK|MB_TASKMODAL|MB_SETFOREGROUND,
				"VirtualDub has detected an improper VBR audio encoding in the source AVI file and will rewrite the audio header "
				"with standard CBR values during processing for better compatibility. This may introduce up to %.0lf ms of skew "
				"from the video stream. If this is unacceptable, decompress the *entire* audio stream to an uncompressed WAV file "
				"and recompress with a constant bitrate encoder. "
				"(bitrate: %.1lf � %.1lf kbps)"
				,maxdev*1000.0,mean*0.001, stddev*0.001);
		}
	}

	return TRUE;
}

void AudioSourceAVI::Reinit() {
	pAVIStream->Info(&streamInfo, sizeof streamInfo);
	lSampleFirst = pAVIStream->Start();
	lSampleLast = pAVIStream->End();
}

bool AudioSourceAVI::isStreaming() {
	return pAVIStream->isStreaming();
}

void AudioSourceAVI::streamBegin(bool fRealTime) {
	pAVIStream->BeginStreaming(lSampleFirst, lSampleLast, fRealTime ? 1000 : 2000);
}

void AudioSourceAVI::streamEnd() {
	pAVIStream->EndStreaming();

}

BOOL AudioSourceAVI::_isKey(LONG lSample) {
	return pAVIStream->IsKeyFrame(lSample);
}
int AudioSourceAVI::_read(LONG lStart, LONG lCount, LPVOID lpBuffer, LONG cbBuffer, LONG *lpBytesRead, LONG *lpSamplesRead) {
	int err;
	long lBytes, lSamples;

	// There are some video clips roaming around with truncated audio streams
	// (audio streams that state their length as being longer than they
	// really are).  We use a kludge here to get around the problem.

	err = pAVIStream->Read(lStart, lCount, lpBuffer, cbBuffer, lpBytesRead, lpSamplesRead);

	if (err != AVIERR_FILEREAD)
		return err;

	// Suspect a truncated stream.
	//
	// AVISTREAMREAD_CONVENIENT will tell us if we're actually encountering a
	// true read error or not.  At least for the AVI handler, it returns
	// AVIERR_ERROR if we've broached the end.  

	*lpBytesRead = *lpSamplesRead = 0;

	while(lCount > 0) {
		err = pAVIStream->Read(lStart, AVISTREAMREAD_CONVENIENT, NULL, 0, &lBytes, &lSamples);

		if (err)
			return 0;

		if (!lSamples) return AVIERR_OK;

		if (lSamples > lCount) lSamples = lCount;

		err = pAVIStream->Read(lStart, lSamples, lpBuffer, cbBuffer, &lBytes, &lSamples);

		if (err)
			return err;

		lpBuffer = (LPVOID)((char *)lpBuffer + lBytes);
		cbBuffer -= lBytes;
		lCount -= lSamples;

		*lpBytesRead += lBytes;
		*lpSamplesRead += lSamples;
	}

	return AVIERR_OK;
}