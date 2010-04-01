/*****************************************************************************
* dump.cpp: A renderer that dumps samples it receives,
*           based on sample code from Microsoft Windows SDK
*****************************************************************************
* Copyright (C) 2009 Zhou Zongyi <zhouzy@os.pku.edu.cn>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
*****************************************************************************/


#include "stdafx.h"

DEFINE_GUID(CLSID_OverlayMixer2,
0xA0025E90, 0xE45B, 0x11D1, 0xAB, 0xE9, 0x0, 0xA0, 0xC9, 0x05, 0xF3, 0x75);

#define EQUAL(x,num) ((x)-(num) < 0.01) && ((x)-(num) > -0.01)
const short  par[][2] = {{8,9},{10,11},{16,15},{12,11},{32,27},{40,33},{4,3},{15,11},{64,45},{16,11},{160,99},{18,11},{20,11},{64,33},{24,11},{80,33},{32,11},{0,0}};
const double parval[] = {8./9.,10./11.,16./15.,12./11.,32./27.,40./33.,4./3.,15./11.,64./45.,16./11.,160./99.,18./11.,20./11.,64./33.,24./11.,80./33.,32./11.};

GUID g_MediaType;
GrabSampleCallbackRoutine g_pCallBack;

WCHAR wszTemp[MAX_PATH];

static REFERENCE_TIME tOffset = 0;

static int inline my_wcsicmp(const WCHAR *s, const WCHAR *ext)
{
	WCHAR c,d = *ext++;
	do {
		c = *s++;
		if ((USHORT)(c-'a') < 26) c -= 'a'-'A';
		if (c != d) return 1;
	}while (d = *ext++);
	return 0;
}

static int inline my_wcscmp(const WCHAR *s, const WCHAR *ext)
{
	WCHAR c,d = *ext++;
	do {
		c = *s++;
		if (c != d) return 1;
	}while (d = *ext++);
	return 0;
}

static inline const WCHAR* my_wcschr(const WCHAR *s, WCHAR c)
{
	do {
		if (*s == c) return s;
		s++;
	} while(*s);
	return NULL;
}

static int inline my_wtoi(const WCHAR *s)
{
	int r = 0;
	USHORT c = *s - '0';
	while (c < 10) {
		r = r*10 + c;
		c = *s++;
	}
	return r;
}

static IBaseFilter *
CreateDumpInstance()
{
	HRESULT hr;
	IBaseFilter *pBF;
	CDump *pDump = new CDump(NULL, &hr);

	if (FAILED(hr))
		return NULL;
	if (FAILED(pDump->NonDelegatingQueryInterface(IID_IBaseFilter, (void **)&pBF))) {
		pDump->Release();
		return NULL;
	}
	return pBF;
}

static HRESULT
LoadGraphFile(IGraphBuilder *pGraph, const WCHAR* wszName)
{
	IStorage *pStorage = 0;
	if (S_OK != StgIsStorageFile(wszName)) return E_FAIL;

	HRESULT hr = StgOpenStorage(wszName, 0, 
								STGM_TRANSACTED | STGM_READ | STGM_SHARE_DENY_WRITE, 
								0, 0, &pStorage);
	if (FAILED(hr))
		return hr;

	IPersistStream *pPersistStream = 0;
	hr = pGraph->QueryInterface(IID_IPersistStream,
			 reinterpret_cast<void**>(&pPersistStream));
	if (SUCCEEDED(hr)) {
		IStream *pStream = 0;
		hr = pStorage->OpenStream(L"ActiveMovieGraph", 0, 
			STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &pStream);
		if(SUCCEEDED(hr)) {
			hr = pPersistStream->Load(pStream);
			pStream->Release();
		}
		pPersistStream->Release();
	}
	pStorage->Release();
	return hr;
}

typedef HRESULT (__stdcall *DllGetClassObjectFunc)(REFCLSID, REFIID, void**);

static HRESULT
LoadIFOFile(IGraphBuilder *pGraph, const WCHAR* wszName)
{
	DllGetClassObjectFunc pDllGetClassObject;
	IFileSourceFilter *pVTSReader;
	static HMODULE hVTSReaderModDLL;
	HRESULT hr;
	if (FAILED(CoCreateInstance(CLSID_MPC_VTSReader, NULL, CLSCTX_INPROC_SERVER,
		IID_IFileSourceFilter, (void **)&pVTSReader))) {
		// try load from dll
		if (!hVTSReaderModDLL) {
			hVTSReaderModDLL = LoadLibraryA("VTSReaderMod.ax");
			if (!hVTSReaderModDLL) return E_FAIL;
		}
		pDllGetClassObject = (DllGetClassObjectFunc)GetProcAddress(hVTSReaderModDLL,"DllGetClassObject");
		if (!pDllGetClassObject) return E_FAIL;
		IClassFactory *pCF;
		if (hr = FAILED(pDllGetClassObject(CLSID_MPC_VTSReader, IID_IClassFactory, (void**)&pCF)))
			return hr;
		if (hr = FAILED(pCF->CreateInstance(NULL, IID_IFileSourceFilter, (void **)&pVTSReader)))
			return hr;
		pCF->Release();
	}
	AM_MEDIA_TYPE pmt;
	IBaseFilter *pBFVTS;
	if (FAILED(hr = pVTSReader->Load(wszName, &pmt)))
		return hr;
	pVTSReader->QueryInterface(IID_IBaseFilter, (void**)&pBFVTS);
	pGraph->AddFilter(pBFVTS, L"VTS Reader");

	IBaseFilter *pBF;
	if (FAILED(CoCreateInstance(CLSID_MPC_MPEGSplitter, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void **)&pBF))) {
			if (!hVTSReaderModDLL) {
				hVTSReaderModDLL = LoadLibraryA("VTSReaderMod.ax");
				if (!hVTSReaderModDLL) return E_FAIL;
			}
			pDllGetClassObject = (DllGetClassObjectFunc)GetProcAddress(hVTSReaderModDLL,"DllGetClassObject");
			if (!pDllGetClassObject) return E_FAIL;
			IClassFactory *pCF;
			if (hr = FAILED(pDllGetClassObject(CLSID_MPC_MPEGSplitter, IID_IClassFactory, (void**)&pCF)))
				return hr;
			if (hr = FAILED(pCF->CreateInstance(NULL, IID_IBaseFilter, (void **)&pBF)))
				return hr;
			pCF->Release();
	}
	pGraph->AddFilter(pBF, L"MPEG Splitter");
	
	IEnumPins *ep;
	IPin *pOut, *pIn;
	pBFVTS->EnumPins(&ep);
	ep->Next(1, &pOut, NULL);
	ep->Release();
	pBF->EnumPins(&ep);
	while (S_OK == (hr = ep->Next(1, &pIn, NULL))) {
		PIN_DIRECTION dir;
		pIn->QueryDirection(&dir);
		if (dir == PINDIR_INPUT) {
			break;
		}
		pIn->Release();
	}
	ep->Release();
	if (hr != S_OK) return E_FAIL;
	if (FAILED(hr = pGraph->Connect(pOut, pIn)))
		return hr;
	pOut->Release();pIn->Release();
	pBF->EnumPins(&ep);
	while (S_OK == ep->Next(1, &pOut, NULL)) {
		PIN_DIRECTION dir;
		pOut->QueryDirection(&dir);
		if (dir == PINDIR_OUTPUT) {
			pGraph->Render(pOut);
		}
		pOut->Release();
	}
	ep->Release();
	return S_OK;
}

void GetInputPinInfo(IBaseFilter *pFilter, const WCHAR* szFileName, char **decoderName)
{
	IEnumPins *pEnum = NULL;
	IPin *pPin = NULL;
	IPin *fpPin = NULL;
	HRESULT hr;
	PIN_INFO pinf;
	FILTER_INFO finf;

	hr = pFilter->EnumPins(&pEnum);
	if (FAILED(hr))
		return;

	if(SUCCEEDED(pEnum->Next(1, &pPin, 0))) {

		if (FAILED(pPin->ConnectedTo(&fpPin))) {
			pPin->Release();
			return;
		}
		if (FAILED(fpPin->QueryPinInfo(&pinf))) {
			fpPin->Release();
			pPin->Release();
			return;
		}
		if (FAILED(pinf.pFilter->QueryFilterInfo(&finf))) {
			fpPin->Release();
			pPin->Release();
			return;
		}

		if(my_wcscmp(finf.achName, szFileName)) {
			char *name = new char[256];
			memset(name, 0, 256);
			WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR, finf.achName, wcslen(finf.achName), name, 256, 0, 0);
			*decoderName = name;
		}

		fpPin->Release();
		pPin->Release();
	}
	pEnum->Release();
}

void GetPinInfo(IPin *inPin, const WCHAR* szFileName, char **decoderName)
{
	PIN_INFO pinf;
	FILTER_INFO finf;

	if (FAILED(inPin->QueryPinInfo(&pinf)))
		return;

	if (FAILED(pinf.pFilter->QueryFilterInfo(&finf)))
		return;
	if(!my_wcscmp(finf.achName, L"0002") || !my_wcscmp(finf.achName, L"DirectVobSub")) {
		GetInputPinInfo(pinf.pFilter, szFileName, decoderName);
	} else if(my_wcscmp(finf.achName, szFileName)) {
		char *name = new char[256];
		memset(name, 0, 256);
		WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR, finf.achName, wcslen(finf.achName), name, 256, 0, 0);
		*decoderName = name;
	}
}

// Setup data
__declspec(naked) pdump_graph_instance_t __stdcall
InitDShowGraphFromFile(const char * szFileName,		// File to play
					   GUID MediaType,				// Preferred media type, see <uuids.h> (e.g. MEDIASUBTYPE_YV12)
					   INT dwVideoID,				// Video ID, -1 for no audio
					   INT dwAudioID,				// Audio ID, -1 for no video
					   GrabSampleCallbackRoutine pVideoCallback,// Callback routine
					   GrabSampleCallbackRoutine pAudioCallback,
					   video_info_t *pVideoInfo,
					   audio_info_t *pAudioInfo)
{
	__asm {
		mov eax, [esp+4]
		test eax, eax
		je INV_FNAME
		mov ecx, MultiByteToWideChar
		push MAX_PATH - 1
		push offset wszTemp[0]
		push -1
		push eax
		push 0
		push CP_OEMCP
		call ecx
		mov DWORD PTR [esp+4], offset wszTemp[0]
INV_FNAME:
		mov eax, InitDShowGraphFromFileW
		jmp eax
	}
}

#define RETERR(x) do{\
if(pVideoInfo) pVideoInfo->reserved = (x);\
else if(pAudioInfo) pAudioInfo->reserved = (x);\
return 0;\
}while(0)

pdump_graph_instance_t __stdcall
InitDShowGraphFromFileW(const WCHAR * szFileName,	// File to play
						GUID MediaType,				// Preferred media type, see <uuids.h> (e.g. MEDIASUBTYPE_YV12)
						INT dwVideoID,				// Video ID, -1 for no audio
						INT dwAudioID,				// Audio ID, -1 for no video
						GrabSampleCallbackRoutine pVideoCallback,// Callback routine
						GrabSampleCallbackRoutine pAudioCallback,
						video_info_t *pVideoInfo,
						audio_info_t *pAudioInfo)
{
	IEnumPins *pEP = NULL;
	IEnumFilters *pEF = NULL;
	IBaseFilter *pVR[16];
	IBaseFilter *pAR[16];
	IBaseFilter *pFR = NULL;
	IPin *pRin = NULL;
	IPin *pVOut = NULL;
	IPin *pVOutSel = NULL;
	IPin *pAOut = NULL;
	IPin *pAOutSel = NULL;
	IBaseFilter *pVNULL = NULL, *pANULL = NULL;
	IAMStreamSelect *pSS = NULL;
	FILTER_INFO fiVR={0};
	PIN_INFO piVDec={0}, piADec={0};
	PIN_DIRECTION pd;
	AM_MEDIA_TYPE mt;
	int dwVCount = 0, dwACount = 0;
	int i,len;
	CLSID clsid;
	tOffset = 0;
	if(pVideoInfo)
		pVideoInfo->videoDecoder = NULL;
	if(pAudioInfo)
		pAudioInfo->audioDecoder = NULL;
	dump_graph_instance_t *pdgi = (dump_graph_instance_t *)CoTaskMemAlloc(sizeof(dump_graph_instance_t));

	pdgi->pDumpV = pdgi->pDumpA = NULL;
	if (FAILED(CoInitialize(NULL)))
		RETERR(ERR_COM);
	// Get the interface for DirectShow's GraphBuilder
	if (FAILED(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, 
								IID_IGraphBuilder, (void **)&pdgi->pGB))) {
		CoUninitialize();
		RETERR(ERR_GRAPH);
	}
	if (!szFileName) {
		SAFE_RELEASE(pdgi->pGB);
		CoTaskMemFree(pdgi);
		CoUninitialize();
		RETERR(ERR_RENDER);
	}
	len = wcslen(szFileName);
	if (len > 4 && !my_wcsicmp(szFileName+len-4,L".GRF")) {
		if (FAILED(LoadGraphFile(pdgi->pGB,szFileName))) {
			SAFE_RELEASE(pdgi->pGB);
			CoTaskMemFree(pdgi);
			CoUninitialize();
			RETERR(ERR_RENDER);
		}
	} else if (len > 4 && !my_wcsicmp(szFileName+len-4,L".IFO")) {
		if (FAILED(LoadIFOFile(pdgi->pGB,szFileName))) {
			SAFE_RELEASE(pdgi->pGB);
			CoTaskMemFree(pdgi);
			CoUninitialize();
			RETERR(ERR_RENDER);
		}
	} else if (FAILED(pdgi->pGB->RenderFile(szFileName,NULL))) {
		SAFE_RELEASE(pdgi->pGB);
		CoTaskMemFree(pdgi);
		CoUninitialize();
		RETERR(ERR_RENDER);
	}
	pdgi->pGB->EnumFilters(&pEF);
	while (S_OK == pEF->Next(1,&pFR,NULL)) {
		pFR->GetClassID(&clsid);
		if (CLSID_VideoRenderer == clsid || CLSID_VideoMixingRenderer == clsid)
			pVR[dwVCount++] = pFR;
		else if (CLSID_DSoundRender == clsid)
			pAR[dwACount++] = pFR;
		else if (!(dwAudioID&0x10000) && CLSID_DMOWrapperFilter == clsid) {
			pFR->QueryFilterInfo(&fiVR);
			if (0 == my_wcscmp(fiVR.achName,L"WMAudio Decoder DMO")) {
				IPropertyBag *pPropertyBag = NULL;
				if(SUCCEEDED(pFR->QueryInterface(IID_IPropertyBag, (void**)&pPropertyBag))) {
					VARIANT myVar;
					IEnumPins* ep;
					VariantInit(&myVar);
					// Enable full output capabilities
					myVar.vt = VT_BOOL;
					myVar.boolVal = -1; // True
					pPropertyBag->Write(L"_HIRESOUTPUT", &myVar);
					pPropertyBag->Release();
					if (SUCCEEDED(pFR->EnumPins(&ep))) {
						IPin* pin;
						// Search for output pin
						while (S_OK == ep->Next(1, &pin, NULL)) {
							PIN_DIRECTION dir;
							pin->QueryDirection(&dir);
							if (dir == PINDIR_OUTPUT) {
								pdgi->pGB->Reconnect(pin);
								pin->Release();
								break;
							}
							pin->Release();
						}
						ep->Release();
					}
				}
			}
		} else if (pAudioCallback && !pSS && (CLSID_MPC_MPEGSplitter == clsid || CLSID_HAALI_Splitter == clsid))
			pFR->QueryInterface(IID_IAMStreamSelect, (void**)&pSS);
		pFR->Release();
	}
	pEF->Release();
	if (dwVCount < dwVideoID+1) {
		if(dwAudioID >= 0) {
			dwVideoID = -1;
			pVideoCallback = NULL;
		} else {
			SAFE_RELEASE(pdgi->pGB);
			CoTaskMemFree(pdgi);
			CoUninitialize();
			RETERR(ERR_VR);
		}
	}
	if (!pSS && dwACount < ((dwAudioID+1)&0xFFFF)) {
		if(dwVideoID >= 0) {
			dwAudioID = -1;
			pAudioCallback = NULL;
		} else {
			SAFE_RELEASE(pdgi->pGB);
			CoTaskMemFree(pdgi);
			CoUninitialize();
			RETERR(ERR_AR);
		}
	}
	for (i=dwVCount-1;i>=0;i--) {
		pVR[i]->EnumPins(&pEP);
		pEP->Next(1,&pRin,NULL);
		pRin->ConnectedTo(&pVOut);
		if (dwVCount-i-1 == dwVideoID) {
			GetPinInfo(pVOut, szFileName, &pVideoInfo->videoDecoder);
			pdgi->pGB->RemoveFilter(pVR[i]);
			pVOut->QueryPinInfo(&piVDec);
			piVDec.pFilter->GetClassID(&clsid);
			// remove Overlay Mixer and annoying AVI Decompresser (aka VFW decoder)
			if (CLSID_OverlayMixer == clsid || CLSID_OverlayMixer2 == clsid || CLSID_AVIDec == clsid) {
				IEnumPins* ep;IPin* pin; IPin* pout;
				piVDec.pFilter->EnumPins(&ep);
				while(S_OK == ep->Next(1,&pin,NULL)) {
					pin->QueryDirection(&pd);
					if (PINDIR_INPUT == pd) {
						pin->ConnectedTo(&pout);
						if (pout) {
							pVOut->Release();
							pVOut = pout;
							pin->Release();
							pdgi->pGB->RemoveFilter(piVDec.pFilter);
							break;
						}
					}
					pin->Release();
				}
				ep->Release();
			}
			g_pCallBack = pVideoCallback;
			g_MediaType = MediaType;
			if (NULL == (pdgi->pDumpV = CreateDumpInstance())) {
				SAFE_RELEASE(pdgi->pGB);
				CoTaskMemFree(pdgi);
				CoUninitialize();
				RETERR(ERR_FILTER);
			}
			pdgi->pGB->AddFilter(pdgi->pDumpV,L"Dump Video");
			pVOutSel = pVOut;
			if (!pSS) {
				pRin->Release();
				pdgi->pDumpV->EnumPins(&pEP);
				pEP->Next(1,&pRin,NULL);
				if (S_OK != pdgi->pGB->Connect(pVOut,pRin)) {
					SAFE_RELEASE(pdgi->pGB);
					CoTaskMemFree(pdgi);
					CoUninitialize();
					RETERR(ERR_TYPE);
				}
			}
		}
		else
		{
			DWORD dwOutPins = 0;
			pVOut->QueryPinInfo(&piVDec);
			piVDec.pFilter->EnumPins(&pEP);
			pdgi->pGB->RemoveFilter(pVR[i]);
			while(S_OK == pEP->Next(1,&pRin,NULL)) {
				pRin->QueryDirection(&pd);
				if (PINDIR_OUTPUT == pd) {
					if (1 == dwOutPins)
						goto VNULL;
					++dwOutPins;
				}
			}
			pdgi->pGB->RemoveFilter(piVDec.pFilter);
			goto NONVSRC;
VNULL:
			// has other output pins, so connect to null renderer
			if (FAILED(CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER,
				IID_IBaseFilter, (void **)&pVNULL))) {
				SAFE_RELEASE(pdgi->pGB);
				CoTaskMemFree(pdgi);
				CoUninitialize();
				RETERR(ERR_FILTER);
			}
			pdgi->pGB->AddFilter(pVNULL,NULL);
			pVNULL->EnumPins(&pEP);
			pEP->Next(1,&pRin,NULL);
			pVOut->Connect(pRin,NULL);
		}
NONVSRC:
		pRin->Release();
		pEP->Release();
		pVR[i]->Release();
	}
	for (i=dwACount-1;i>=0;i--) {
		pAR[i]->EnumPins(&pEP);
		pEP->Next(1,&pRin,NULL);
		pRin->ConnectedTo(&pAOut);
		if (pSS || dwACount-i-1 == (dwAudioID&0xFFFF)) {
			GetPinInfo(pAOut, szFileName, &pAudioInfo->audioDecoder);
			g_pCallBack = pAudioCallback;
			g_MediaType = MEDIASUBTYPE_NULL;
			if (NULL == (pdgi->pDumpA = CreateDumpInstance())) {
				SAFE_RELEASE(pdgi->pGB);
				CoTaskMemFree(pdgi);
				CoUninitialize();
				RETERR(ERR_FILTER);
			}
			pdgi->pGB->AddFilter(pdgi->pDumpA,L"Dump Audio");
			pAOutSel = pAOut;
			pdgi->pGB->RemoveFilter(pAR[i]);
			if (pSS) goto ANULL;
			pRin->Release();
			pdgi->pDumpA->EnumPins(&pEP);
			pEP->Next(1,&pRin,NULL);
			g_MediaType = MEDIASUBTYPE_NULL;
			if (S_OK != pdgi->pGB->Connect(pAOut,pRin)) {
				SAFE_RELEASE(pdgi->pGB);
				CoTaskMemFree(pdgi);
				CoUninitialize();
				RETERR(ERR_TYPE);
			}
		} else {
			DWORD dwOutPins = 0;
			pAOut->QueryPinInfo(&piADec);
			pdgi->pGB->RemoveFilter(pAR[i]);
			piADec.pFilter->EnumPins(&pEP);
			while(S_OK == pEP->Next(1,&pRin,NULL))
			{
				pRin->QueryDirection(&pd);
				if (PINDIR_OUTPUT == pd) {
					if (1 == dwOutPins)
						goto ANULL;
					++dwOutPins;
				}
			}
			pdgi->pGB->RemoveFilter(piADec.pFilter);
			goto NONASRC;
ANULL:
			// no input pin, connect to null renderer
			if (FAILED(CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER,
				IID_IBaseFilter, (void **)&pANULL))) {
				SAFE_RELEASE(pdgi->pGB);
				CoTaskMemFree(pdgi);
				CoUninitialize();
				RETERR(ERR_FILTER);
			}
			pdgi->pGB->AddFilter(pANULL,NULL);
			pANULL->EnumPins(&pEP);
			pEP->Next(1,&pRin,NULL);
			pAOut->Connect(pRin,NULL);
		}
NONASRC:
		pRin->Release();
		pEP->Release();
		pAR[i]->Release();
	}

	if (S_OK != pdgi->pGB->QueryInterface(IID_IMediaControl,(void **)&pdgi->pMC)
	 || S_OK != pdgi->pGB->QueryInterface(IID_IMediaSeeking,(void **)&pdgi->pMS)) {
		SAFE_RELEASE(pdgi->pGB);
		CoTaskMemFree(pdgi);
		CoUninitialize();
		RETERR(ERR_GRAPH);
	}

	if (pSS) {
		DWORD strmcount = 0;
		AM_MEDIA_TYPE *pmt;
		OAFilterState fs;
		REFERENCE_TIME ts = 0;
		DWORD j = (dwAudioID & 0xF);
		pSS->Count(&strmcount);
		for (i = 0; i < (int)strmcount;i++) {
			pSS->Info(i,&pmt, NULL, NULL, NULL, NULL, NULL, NULL);
			if (pmt->majortype == MEDIATYPE_Audio && !j--) {
				DeleteMediaType(pmt);
				pSS->Enable(i, AMSTREAMSELECTENABLE_ENABLE);
				break;
			}
			DeleteMediaType(pmt);
		}
		pSS->Release();
		pdgi->pMC->Pause();
		pdgi->pMC->GetState(INFINITE,&fs);
		pdgi->pMC->Stop();
		pdgi->pMS->SetPositions(&ts, AM_SEEKING_AbsolutePositioning | AM_SEEKING_NoFlush, NULL, AM_SEEKING_NoPositioning);
		if (pVOutSel) {
			pdgi->pDumpV->EnumPins(&pEP);
			pEP->Next(1,&pRin,NULL);
			if (S_OK != pdgi->pGB->Connect(pVOut,pRin)) {
				SAFE_RELEASE(pdgi->pGB);
				CoTaskMemFree(pdgi);
				CoUninitialize();
				RETERR(ERR_TYPE);
			}
			pRin->Release();
			pEP->Release();
		}
		if (pAOutSel) {
			pAOutSel->ConnectedTo(&pRin);
			pRin->QueryPinInfo(&piADec);
			pdgi->pGB->RemoveFilter(piADec.pFilter);
			pRin->Release();
			pdgi->pDumpA->EnumPins(&pEP);
			pEP->Next(1,&pRin,NULL);
			if (S_OK != pdgi->pGB->Connect(pAOut,pRin)) {
				SAFE_RELEASE(pdgi->pGB);
				CoTaskMemFree(pdgi);
				CoUninitialize();
				RETERR(ERR_TYPE);
			}
			pRin->Release();
			pEP->Release();
		}
	}
#ifdef REGISTER_FILTERGRAPH
	AddGraphToRot(pdgi->pGB, &pdgi->dwGraphRegister);
#endif

	// Retrieve video info
	if (-1 != dwVideoID) {
		pVOutSel->ConnectionMediaType(&mt);
		if (FORMAT_VideoInfo2 == mt.formattype) {
			pVideoInfo->width = ((VIDEOINFOHEADER2 *)mt.pbFormat)->bmiHeader.biWidth;
			pVideoInfo->height = ((VIDEOINFOHEADER2 *)mt.pbFormat)->bmiHeader.biHeight;
			DWORD i = ((VIDEOINFOHEADER2 *)mt.pbFormat)->dwPictAspectRatioX * pVideoInfo->height,
				  j = ((VIDEOINFOHEADER2 *)mt.pbFormat)->dwPictAspectRatioY * pVideoInfo->width;
			do {
				if (!(j%=i)) {
					j = i;
					break;
				}
			} while (i%=j);
			i = ((VIDEOINFOHEADER2 *)mt.pbFormat)->dwPictAspectRatioX * pVideoInfo->height / j;
			j = ((VIDEOINFOHEADER2 *)mt.pbFormat)->dwPictAspectRatioY * pVideoInfo->width / j;
			if (i>255 || j>255) {
				double t = (double)i / (double)j;
				DWORD k = 0;
				i = 0; j = 0;
				for (;par[k][0];k++) {
					if (EQUAL(t,parval[k])) {
						i = par[k][0];
						j = par[k][1];
						break;
					}
				}
			}
			if (pVideoInfo->width == 720) {// Fix incorrect DVD AR returned by decoder
				const WCHAR *ext = szFileName+len-4;
				if (!my_wcsicmp(ext,L".IFO") || !my_wcsicmp(ext,L".VOB")
				 || !my_wcsicmp(ext,L".MPG") || !my_wcsicmp(ext,L".M2V")) {
					if (pVideoInfo->height == 480) {// NTSC
						if (i == 8 && j == 9) {
							i = 10; j = 11;
						} else if (i == 32 && j == 27) {
							i = 40; j = 33;
						}
					} else if (pVideoInfo->height == 576) {// PAL/SECAM
						if (i == 16 && j == 15) {
							i = 12; j = 11;
						} else if (i == 64 && j ==45) {
							i = 16; j = 11;
						}
					}
				}
			}
			pVideoInfo->aspectX = i;
			pVideoInfo->aspectY = j;
			pVideoInfo->avgtimeperframe = ((VIDEOINFOHEADER2 *)mt.pbFormat)->AvgTimePerFrame;
		} else {
			pVideoInfo->width = ((VIDEOINFOHEADER *)mt.pbFormat)->bmiHeader.biWidth;
			pVideoInfo->height = ((VIDEOINFOHEADER *)mt.pbFormat)->bmiHeader.biHeight;
			pVideoInfo->aspectX = 0;
			pVideoInfo->aspectY = 0;
			pVideoInfo->avgtimeperframe = ((VIDEOINFOHEADER *)mt.pbFormat)->AvgTimePerFrame;
		}
		pVideoInfo->haveVideo = 1;
		pVOutSel->Release();
	} else
		pVideoInfo->haveVideo = 0;
	// Retrieve audio info
	if (-1 != dwAudioID) {
		pAOutSel->ConnectionMediaType(&mt);
		pAudioInfo->nSamplesPerSec = ((WAVEFORMATEX *)mt.pbFormat)->nSamplesPerSec;
		pAudioInfo->nChannels = ((WAVEFORMATEX *)mt.pbFormat)->nChannels;
		pAudioInfo->wBitsPerSample = ((WAVEFORMATEX *)mt.pbFormat)->wBitsPerSample;
		pAudioInfo->wFormatTag = ((WAVEFORMATEX *)mt.pbFormat)->wFormatTag;
		pAudioInfo->haveAudio = 1;
		pAOutSel->Release();
	} else
		pAudioInfo->haveAudio = 0;
	return pdgi;
}

double __stdcall
GetGraphDuration(dump_graph_instance_t *pdgi)
{
	DWORD t;
	pdgi->pMS->GetCapabilities(&t);
	if (t & AM_SEEKING_CanGetDuration) {
		REFERENCE_TIME rt;
		pdgi->pMS->GetDuration(&rt);
		return (double)(rt / 1E7);
	}
	return -1;
}

int __stdcall
StartGraph(dump_graph_instance_t *pdgi)
{
	return pdgi->pMC->Run();
}

int __stdcall
StopGraph(dump_graph_instance_t *pdgi)
{
	pdgi->pMC->Stop();
	return 1;
}

int __stdcall
DestroyGraph(dump_graph_instance_t *pdgi)
{
#ifdef REGISTER_FILTERGRAPH
	if (pdgi->dwGraphRegister)
		RemoveGraphFromRot(pdgi->dwGraphRegister);
#endif
	SAFE_RELEASE(pdgi->pDumpV);
	SAFE_RELEASE(pdgi->pDumpA);
	SAFE_RELEASE(pdgi->pMS);
	SAFE_RELEASE(pdgi->pMC);
	SAFE_RELEASE(pdgi->pGB);
	CoTaskMemFree(pdgi);
//	CoUninitialize();
	return 1;
}

int __stdcall
SeekGraph(dump_graph_instance_t *pdgi, REFERENCE_TIME timestamp)
{
	HRESULT hr = pdgi->pMS->SetPositions(&timestamp, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
	if (SUCCEEDED(hr)) tOffset = timestamp;
	return hr;
}


// Constructor

CDumpFilter::CDumpFilter(CDump *pDump,
						 LPUNKNOWN pUnk,
						 CCritSec *pLock,
						 HRESULT *phr) :
	CBaseFilter(NAME("CDumpFilter"), pUnk, pLock, CLSID_Dump),
	m_pDump(pDump)
{
}

//
// GetPin
//
CBasePin * CDumpFilter::GetPin(int n)
{
	if (n == 0)
		return m_pDump->m_pPin;
	else
		return NULL;
}

//
// GetPinCount
//
int CDumpFilter::GetPinCount()
{
	return 1;
}

//
// Stop
//
// Overriden to close the dump file
//
STDMETHODIMP CDumpFilter::Stop()
{
	CAutoLock cObjectLock(m_pLock);
	return CBaseFilter::Stop();
}

//
// Pause
//
// Overriden to open the dump file
//
STDMETHODIMP CDumpFilter::Pause()
{
	CAutoLock cObjectLock(m_pLock);
	return CBaseFilter::Pause();
}

//
// Run
//
// Overriden to open the dump file
//
STDMETHODIMP CDumpFilter::Run(REFERENCE_TIME tStart)
{
	CAutoLock cObjectLock(m_pLock);
	return CBaseFilter::Run(tStart);
}


//
//  Definition of CDumpInputPin
//
CDumpInputPin::CDumpInputPin(CDump *pDump,
							 LPUNKNOWN pUnk,
							 CBaseFilter *pFilter,
							 CCritSec *pLock,
							 CCritSec *pReceiveLock,
							 HRESULT *phr) :

	CRenderedInputPin(NAME("CDumpInputPin"),
				  pFilter,		// Filter
				  pLock,		// Locking
				  phr,			// Return code
				  L"Input"),	// Pin name
	m_pReceiveLock(pReceiveLock),
	m_pDump(pDump),
#ifdef _DEBUG
	m_tLast(0),
#endif
	m_pCallBack(g_pCallBack)
{
}

//
// CheckMediaType
//
// Check if the pin can support this specific proposed type and format
//
HRESULT CDumpInputPin::CheckMediaType(const CMediaType * mt)
{
	if ( g_MediaType == MEDIASUBTYPE_NULL || *mt->Subtype() == g_MediaType)
		return S_OK;
	else
		return S_FALSE;
}

//
// BreakConnect
//
// Break a connection
//
HRESULT CDumpInputPin::BreakConnect()
{
	if (m_pDump->m_pPosition != NULL)
		m_pDump->m_pPosition->ForceRefresh();
	return CRenderedInputPin::BreakConnect();
}


//
// ReceiveCanBlock
//
// We don't hold up source threads on Receive
//
STDMETHODIMP CDumpInputPin::ReceiveCanBlock()
{
	return S_OK;
}

//
// Receive
//
// Do something with this media sample
//
STDMETHODIMP CDumpInputPin::Receive(IMediaSample *pSample)
{
	CAutoLock lock(m_pReceiveLock);
	PBYTE pbData;

	REFERENCE_TIME tStart, tStop;
	pSample->GetTime(&tStart, &tStop);

#ifdef _DEBUG
	DbgLog((LOG_TRACE, 1, TEXT("tStart(%s), tStop(%s), Diff(%d ms), Bytes(%d)"),
		   (LPCTSTR) CDisp(tStart),
		   (LPCTSTR) CDisp(tStop),
		   (LONG)((tStart - m_tLast) / 10000),
		   pSample->GetActualDataLength()));

	m_tLast = tStart;
#endif

	// Copy the data to the file
	HRESULT hr = pSample->GetPointer(&pbData);
	if (FAILED(hr))
		return hr;

	if(tOffset) tStart += tOffset;
	return m_pCallBack(pbData,pSample->GetActualDataLength(),tStart)? S_OK: S_FALSE;
}

//
// EndOfStream
//
STDMETHODIMP CDumpInputPin::EndOfStream(void)
{
	CAutoLock lock(m_pReceiveLock);
	m_pCallBack(NULL,0,0);
	return CRenderedInputPin::EndOfStream();
} // EndOfStream

#ifdef _DEBUG
//
// NewSegment
//
// Called when we are seeked
//
STDMETHODIMP CDumpInputPin::NewSegment(REFERENCE_TIME tStart,
									   REFERENCE_TIME tStop,
									   double dRate)
{
	m_tLast = 0;
	return S_OK;

} // NewSegment
#endif


//  Constructor

CDump::CDump(LPUNKNOWN pUnk, HRESULT *phr) :
	CUnknown(NAME("CDump"), pUnk),
	m_pFilter(NULL),
	m_pPin(NULL),
	m_pPosition(NULL)
{
	m_pFilter = new CDumpFilter(this, GetOwner(), &m_Lock, phr);
	if (m_pFilter == NULL) {
		*phr = E_OUTOFMEMORY;
		return;
	}

	m_pPin = new CDumpInputPin(this,GetOwner(),
							   m_pFilter,
							   &m_Lock,
							   &m_ReceiveLock,
							   phr);
	if (m_pPin == NULL) {
		*phr = E_OUTOFMEMORY;
		return;
	}
}

// Destructor

CDump::~CDump()
{
	delete m_pPin;
	delete m_pFilter;
	delete m_pPosition;
}

//
// NonDelegatingQueryInterface
//
// Override this to say what interfaces we support where
//
STDMETHODIMP CDump::NonDelegatingQueryInterface(REFIID riid, void ** ppv)
{
	CheckPointer(ppv,E_POINTER);
	CAutoLock lock(&m_Lock);

	if (riid == IID_IBaseFilter || riid == IID_IMediaFilter || riid == IID_IPersist) {
		return m_pFilter->NonDelegatingQueryInterface(riid, ppv);
	} 
	else if (riid == IID_IMediaPosition || riid == IID_IMediaSeeking) {
		if (m_pPosition == NULL) {

			HRESULT hr = S_OK;
			m_pPosition = new CPosPassThru(NAME("Dump Pass Through"),
										   (IUnknown *) GetOwner(),
										   (HRESULT *) &hr, m_pPin);
			if (m_pPosition == NULL) {
				return E_OUTOFMEMORY;
			}

			if (FAILED(hr)) {
				delete m_pPosition;
				m_pPosition = NULL;
				return hr;
			}
		}

		return m_pPosition->NonDelegatingQueryInterface(riid, ppv);
	} 

	return CUnknown::NonDelegatingQueryInterface(riid, ppv);

} // NonDelegatingQueryInterface

#ifdef REGISTER_FILTERGRAPH
HRESULT AddGraphToRot(IUnknown *pUnkGraph, DWORD *pdwRegister) 
{
	IMoniker * pMoniker;
	IRunningObjectTable *pROT;
	WCHAR wsz[128];

	if (FAILED(GetRunningObjectTable(0, &pROT))) 
		return E_FAIL;

	swprintf_s(wsz, 128, L"FilterGraph %08x pid %08x", (DWORD_PTR)pUnkGraph, 
			   GetCurrentProcessId());

	HRESULT hr = CreateItemMoniker(L"!", wsz, &pMoniker);
	if (SUCCEEDED(hr)) {
		hr = pROT->Register(0, pUnkGraph, pMoniker, pdwRegister);
		pMoniker->Release();
	}

	pROT->Release();
	return hr;
}

void RemoveGraphFromRot(DWORD pdwRegister)
{
	IRunningObjectTable *pROT;

	if (SUCCEEDED(GetRunningObjectTable(0, &pROT))) {
		pROT->Revoke(pdwRegister);
		pROT->Release();
	}
}
#endif
