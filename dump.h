#define SAFE_RELEASE(x) { if (x) x->Release(); x = NULL; }
#define REGISTER_FILTERGRAPH

enum INITERROR {
	ERR_COM=1,
	ERR_GRAPH,
	ERR_RENDER,
	ERR_VR,
	ERR_AR,
	ERR_FILTER,
	ERR_TYPE
};

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

typedef struct {
	IGraphBuilder *pGB;
	IMediaControl *pMC;
	IMediaSeeking *pMS;
	IBaseFilter *pDumpV;
	IBaseFilter *pDumpA;
	DWORD dwGraphRegister;
} dump_graph_instance_t, *pdump_graph_instance_t;

typedef int (__stdcall *GrabSampleCallbackRoutine)
	(PBYTE pData,			// Point to sample data
	 LONG iLen,				// Length of sample data
	 LONGLONG i64TimeStamp);// Timestamp of sample, in 1/10000000 sec.

extern "C" pdump_graph_instance_t __stdcall
InitDShowGraphFromFile(const char * szFileName,		// File to play
					   GUID MediaType,				// Preferred media type, see <uuids.h> (e.g. MEDIASUBTYPE_YV12)
					   INT dwVideoID,				// Video ID, -1 for no audio
					   INT dwAudioID,				// Audio ID, -1 for no video
					   GrabSampleCallbackRoutine pVideoCallback,// Callback routine
					   GrabSampleCallbackRoutine pAudioCallback,
					   video_info_t *pVideoInfo,
					   audio_info_t *pAudioInfo);
extern "C" pdump_graph_instance_t __stdcall
InitDShowGraphFromFileW(const WCHAR * szFileName,	// File to play
						GUID MediaType,				// Preferred media type, see <uuids.h> (e.g. MEDIASUBTYPE_YV12)
						INT dwVideoID,				// Video ID, -1 for no audio
						INT dwAudioID,				// Audio ID, -1 for no video
						GrabSampleCallbackRoutine pVideoCallback,// Callback routine
						GrabSampleCallbackRoutine pAudioCallback,
						video_info_t *pVideoInfo,
						audio_info_t *pAudioInfo);
extern "C" int __stdcall
StartGraph(dump_graph_instance_t *pdgi);
extern "C" int __stdcall
StopGraph(dump_graph_instance_t *pdgi);
extern "C" int __stdcall
SeekGraph(dump_graph_instance_t *pdgi, REFERENCE_TIME timestamp);
extern "C" int __stdcall
DestroyGraph(dump_graph_instance_t *pdgi);
extern "C" double __stdcall
GetGraphDuration(dump_graph_instance_t *pdgi);

#ifdef REGISTER_FILTERGRAPH
HRESULT AddGraphToRot(IUnknown *pUnkGraph, DWORD *pdwRegister);
void RemoveGraphFromRot(DWORD pdwRegister);
#endif

// Main filter object

class CDump;
class CDumpFilter : public CBaseFilter
{
	CDump * const m_pDump;

public:

	// Constructor
	CDumpFilter(CDump *pDump,
				LPUNKNOWN pUnk,
				CCritSec *pLock,
				HRESULT *phr);

	// Pin enumeration
	CBasePin * GetPin(int n);
	int GetPinCount();

	// Open and close the file as necessary
	STDMETHODIMP Run(REFERENCE_TIME tStart);
	STDMETHODIMP Pause();
	STDMETHODIMP Stop();
};


//  Pin object

class CDumpInputPin : public CRenderedInputPin
{
	CDump *const m_pDump;			// Main renderer object
	CCritSec *const m_pReceiveLock;	// Sample critical section
	GrabSampleCallbackRoutine m_pCallBack;

public:

    CDumpInputPin(CDump *pDump,
                  LPUNKNOWN pUnk,
                  CBaseFilter *pFilter,
                  CCritSec *pLock,
                  CCritSec *pReceiveLock,
                  HRESULT *phr);

	// Do something with this media sample
	STDMETHODIMP Receive(IMediaSample *pSample);
	STDMETHODIMP EndOfStream(void);
	STDMETHODIMP ReceiveCanBlock();

	// Check if the pin can support this specific proposed type and format
	HRESULT CheckMediaType(const CMediaType *);

	// Break connection
	HRESULT BreakConnect();

#ifdef _DEBUG
	// Track NewSegment
	STDMETHODIMP NewSegment(REFERENCE_TIME tStart,
							REFERENCE_TIME tStop,
							double dRate);
private:
	REFERENCE_TIME m_tLast;	// Last sample receive time
#endif
};


//  CDump object which has filter and pin members

class CDump : public CUnknown//, public IFileSinkFilter
{
	friend class CDumpFilter;
	friend class CDumpInputPin;

	CDumpFilter *m_pFilter;		// Methods for filter interfaces
	CDumpInputPin *m_pPin;		// A simple rendered input pin
	CCritSec m_Lock;			// Main renderer critical section
	CCritSec m_ReceiveLock;		// Sublock for received samples
	CPosPassThru *m_pPosition;	// Renderer position controls

public:

	DECLARE_IUNKNOWN

    CDump(LPUNKNOWN pUnk, HRESULT *phr);
	~CDump();

	// Overriden to say what interfaces we support where
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void ** ppv);
};
