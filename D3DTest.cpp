#include "StdAfx.h"
#include "D3DTest.h"
#include <D3D9.h>
#include <stdio.h>
#include "d3dx9api.h"

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(x)  if(x) {x->Release(); x=0;}
#endif

CD3DTest::CD3DTest(void)
{
}

CD3DTest::~CD3DTest(void)
{
}

int RGB2YUV(void* pRGB, void* pYUV, int size)
{
	if (!pRGB || !pYUV)
		return -1;

	unsigned char* pRGBData = (unsigned char *)pRGB;
	unsigned char* pYUVData = (unsigned char *)pYUV;

	int R1, G1, B1, R2, G2, B2, Y1, U1, Y2, V1;
	for (int i=0; i<size/4; ++i)
	{
		B1 = *pRGBData;
		G1 = *(pRGBData+1);
		R1 = *(pRGBData+2);
		B2 = *(pRGBData+3);
		G2 = *(pRGBData+4);
		R2 = *(pRGBData+5);
		
		Y1 = (((66*R1+129*G1+25*B1+128)>>8) + 16) > 255 ? 255 : (((66*R1+129*G1+25*B1+128)>>8) + 16);
		U1 = ((((-38*R1-74*G1+112*B1+128)>>8)+((-38*R2-74*G2+112*B2+128)>>8))/2 + 128)>255 ? 255 : ((((-38*R1-74*G1+112*B1+128)>>8)+((-38*R2-74*G2+112*B2+128)>>8))/2 + 128);
		Y2 = (((66*R2+129*G2+25*B2+128)>>8) + 16)>255 ? 255 : ((66*R2+129*G2+25*B2+128)>>8) + 16;
		V1 = ((((112*R1-94*G1-18*B1+128)>>8) + ((112*R2-94*G2-18*B2+128)>>8))/2 + 128)>255 ? 255 : ((((112*R1-94*G1-18*B1+128)>>8) + ((112*R2-94*G2-18*B2+128)>>8))/2 + 128);
		
		*pYUVData = Y1;
		*(pYUVData+1) = U1;
		*(pYUVData+2) = Y2;
		*(pYUVData+3) = V1;

		pRGBData += 6;
		pYUVData += 4;
	}

	return 0;
}

typedef IDirect3D9 * (WINAPI *ImpDirect3DCreate9)(UINT);

int CD3DTest::isFullRange(HWND hWnd)
{
	int fullrange = 0;
	HMODULE d3dx9_dll;
	char dll_str[32];
	IDirect3DSurface9*   pBuf = NULL;
	IDirect3DSurface9*   pSur = NULL;
	IDirect3DTexture9*   pTer = NULL;
	IDirect3DDevice9*    pd3dDevice;
	D3DLOCKED_RECT locked_rect;
	LPD3DXBUFFER ppDestBuf;
	ImpDirect3DCreate9 pDirect3DCreate9;
	//D3DXSaveSurfaceToFilePtr pD3DXSaveSurfaceToFile;
	D3DXSaveSurfaceToFileInMemoryPtr pD3DXSaveSurfaceToFileInMemory;

	// load latest compatible version of the DLL that is available
	for (int i=43; i>=24; i--) {
		sprintf_s(dll_str, 32, "d3dx9_%d.dll", i);
		d3dx9_dll = LoadLibraryA(dll_str);
		if (d3dx9_dll) break;
	}

	if(!d3dx9_dll)
		return 0;

	HMODULE d3d9_dll = LoadLibraryA("d3d9.dll");

	if(!d3d9_dll) {
		FreeLibrary(d3dx9_dll);
		return 0;
	}

	//pD3DXSaveSurfaceToFile = (D3DXSaveSurfaceToFilePtr) GetProcAddress(d3dx9_dll, "D3DXSaveSurfaceToFileW");
	pD3DXSaveSurfaceToFileInMemory = (D3DXSaveSurfaceToFileInMemoryPtr) GetProcAddress(d3dx9_dll, "D3DXSaveSurfaceToFileInMemory");

	pDirect3DCreate9 = (ImpDirect3DCreate9) GetProcAddress(d3d9_dll, "Direct3DCreate9");
	IDirect3D9 *m_pD3D9 = pDirect3DCreate9( D3D_SDK_VERSION ); 

	int width = 4;
	int height = 4;

	D3DDISPLAYMODE m_displayMode;
	m_pD3D9->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &m_displayMode);

	D3DCAPS9 deviceCaps;
	m_pD3D9->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &deviceCaps);

	DWORD dwBehaviorFlags = D3DCREATE_MULTITHREADED;

	if(deviceCaps.VertexProcessingCaps != 0 )
		dwBehaviorFlags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
	else
		dwBehaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	D3DPRESENT_PARAMETERS presentParams = {0};
	presentParams.Flags                  = D3DPRESENTFLAG_VIDEO;
	presentParams.Windowed               = TRUE;
	presentParams.hDeviceWindow          = hWnd;
	presentParams.BackBufferWidth        = width;
	presentParams.BackBufferHeight       = height;
	presentParams.SwapEffect             = D3DSWAPEFFECT_COPY;
	presentParams.MultiSampleType        = D3DMULTISAMPLE_NONE;
	presentParams.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;
	presentParams.BackBufferFormat       = m_displayMode.Format;
	presentParams.BackBufferCount        = 1;
	presentParams.EnableAutoDepthStencil = FALSE;

	m_pD3D9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, dwBehaviorFlags, &presentParams, &pd3dDevice);

	pd3dDevice->CreateOffscreenPlainSurface( width, height, D3DFMT_YUY2, D3DPOOL_DEFAULT, &pSur, NULL );

	char *rgb = new char[width*height*9];
	memset(rgb, 255, width*height*9);

	pSur->LockRect(&locked_rect, NULL, 0);
	
	RGB2YUV(rgb, locked_rect.pBits, locked_rect.Pitch);
	delete rgb;

	pSur->UnlockRect();

	pd3dDevice->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, m_displayMode.Format, D3DPOOL_DEFAULT, &pTer, NULL);
	
	pTer->GetSurfaceLevel(0, &pBuf);

	if( pBuf != NULL ) {
		HRESULT res = pd3dDevice->StretchRect(pSur, NULL, pBuf, NULL, D3DTEXF_NONE);
		//pD3DXSaveSurfaceToFile(L"test1.tga", D3DXIFF_TGA, pBuf, NULL, NULL );
		if(res == D3D_OK) {
			if( D3D_OK == pD3DXSaveSurfaceToFileInMemory( &ppDestBuf, D3DXIFF_TGA, pBuf, NULL, NULL)) {
				unsigned char *rgbx = (unsigned char *)ppDestBuf->GetBufferPointer();
				if(rgbx && ppDestBuf->GetBufferSize() > 40) {
					for(int i = 33; i < 39; i++) {
						if(rgbx[i] != 235) {
							fullrange = 1;
							break;
						}
					}

				}
				SAFE_RELEASE(ppDestBuf);
			}
		}

		SAFE_RELEASE(pBuf);
	}

	SAFE_RELEASE(pTer);
	SAFE_RELEASE(pd3dDevice);
	SAFE_RELEASE(m_pD3D9);

	FreeLibrary(d3dx9_dll);
	FreeLibrary(d3d9_dll);

	return fullrange;
}

extern "C" unsigned int WINAPI D3DFullColorRange(void)
{
	CD3DTest d3dTester;

	int ret = d3dTester.isFullRange();

	ret = d3dTester.isFullRange();

	return ret;
}
