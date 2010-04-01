// dumptest.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <windows.h>
int __cdecl _wtoi(const wchar_t *);

const GUID MEDIASUBTYPE_None = {0xe436eb8e, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70};
const GUID MEDIASUBTYPE_YV12 = {0x32315659, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};

typedef struct {
	DWORD width : 12;
	DWORD height : 12;
	DWORD aspectX : 8;
	DWORD aspectY : 8;
	DWORD avgtimeperframe : 20;
	DWORD reserved : 4;
	DWORD haveVideo;
	char *videoDecoder;
} video_info_t;

typedef struct {
	DWORD nSamplesPerSec;
	DWORD nChannels;
	DWORD wBitsPerSample;
	DWORD wFormatTag;
	DWORD reserved;
	DWORD haveAudio;
	char *audioDecoder;
} audio_info_t;

typedef void* (__stdcall *TInitDShowGraphFromFile)
(const wchar_t *szFileName, GUID MediaType, DWORD dwVideoID, DWORD dwAudioID,
 void *pVideoCallback, void *pAudioCallback, video_info_t *pdwVideoInfo, audio_info_t *pAudioInfo);

typedef int (__stdcall *TGraphOperate)(void *pdgi);

typedef struct {
	DWORD RIFF,size,WAVE,fmt,format_length;
	SHORT format_tag,channels;
	DWORD sample_rate,avg_bytes_sec;
	SHORT block_align,bits_per_sample;
	DWORD id,data_size;
} TWaveHeader;
#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 1
#endif

TWaveHeader waveheader = {'FFIR',36,'EVAW',' tmf',0x10,
WAVE_FORMAT_PCM,2,48000,48000*4,4,16,'atad',0};

TInitDShowGraphFromFile InitDShowGraphFromFile;
TGraphOperate StartGraph, StopGraph, DestroyGraph;
HANDLE StdOut,hEvent; DWORD dwWritten,ctrlc=0;
void *g_pdgi;

BOOL CALLBACK HandlerRoutine(DWORD dwCtrlType)
{
	switch(dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		ctrlc=1;
		return TRUE;
	}
	return FALSE;
}

int __stdcall VCallBackProc(PBYTE pData, LONG iLen, LONGLONG i64TimeStamp)
{
#ifdef _DEBUG
#else // _DEBUG
#endif // _DEBUG
		return 1;
}

int __stdcall CallBackProc(PBYTE pData, LONG iLen, LONGLONG i64TimeStamp)
{
	if (pData && !ctrlc) {
#ifdef _DEBUG
		printf("%I64u, %d\n", i64TimeStamp, iLen);
#else // _DEBUG
		WriteFile(StdOut,pData,iLen,&dwWritten,NULL);
#endif // _DEBUG
		return 1;
	}
	else {
		SetEvent(hEvent);
		return 0;
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	DWORD aid = 0;
	video_info_t VideoInfo;
	audio_info_t AudioInfo;
	HMODULE hDLL;
	if (argc < 2)
		return 0;
	if (argc > 2)
		aid = _wtoi(argv[2]);
	if (NULL == (hDLL = LoadLibraryA("dshownative.dll")))
		return 0;
	InitDShowGraphFromFile = (TInitDShowGraphFromFile)GetProcAddress(hDLL,"InitDShowGraphFromFileW");
	StartGraph = (TGraphOperate)GetProcAddress(hDLL,"StartGraph");
	StopGraph = (TGraphOperate)GetProcAddress(hDLL,"StopGraph");
	DestroyGraph = (TGraphOperate)GetProcAddress(hDLL,"DestroyGraph");
	if (!(g_pdgi = InitDShowGraphFromFile(argv[1],MEDIASUBTYPE_YV12,0,0,VCallBackProc,CallBackProc,&VideoInfo,&AudioInfo)))
		return 0;
#ifndef _DEBUG
	waveheader.channels = AudioInfo.nChannels;
	waveheader.sample_rate = AudioInfo.nSamplesPerSec;
	waveheader.bits_per_sample = AudioInfo.wBitsPerSample;
	waveheader.block_align = waveheader.channels * waveheader.bits_per_sample / 8;
	waveheader.avg_bytes_sec = waveheader.block_align * waveheader.sample_rate;
	waveheader.format_tag = 0x3;
	StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	WriteFile(StdOut,&waveheader,sizeof(TWaveHeader),&dwWritten,NULL);
#endif
	SetConsoleCtrlHandler(HandlerRoutine, TRUE);
	hEvent = CreateEventA(NULL,FALSE,FALSE,NULL);
	StartGraph(g_pdgi);
	WaitForSingleObject(hEvent,INFINITE);
	StopGraph(g_pdgi);DestroyGraph(g_pdgi);
	if (GetFileType(StdOut) == FILE_TYPE_DISK) {
		waveheader.data_size = GetFileSize(StdOut,NULL) - sizeof(TWaveHeader);
		waveheader.size = waveheader.data_size + sizeof(TWaveHeader) - 8;
		SetFilePointer(StdOut,0,NULL,FILE_BEGIN);
		WriteFile(StdOut,&waveheader,sizeof(TWaveHeader),&dwWritten,NULL);
	}
	CloseHandle(StdOut);
	CoUninitialize();
	return 1;
}

