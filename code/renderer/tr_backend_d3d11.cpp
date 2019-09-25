/*
===========================================================================
Copyright (C) 2019 Gian 'myT' Schellenbaum

This file is part of Challenge Quake 3 (CNQ3).

Challenge Quake 3 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Challenge Quake 3 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Challenge Quake 3. If not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/
// Direct3D 11 rendering back-end

#if defined(_WIN32)


#include "tr_local.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "hlsl/generic_vs.h"
#include "hlsl/generic_ps.h"
#include "hlsl/generic_a_ps.h"
#include "hlsl/generic_d_ps.h"
#include "hlsl/generic_ad_ps.h"
#include "hlsl/post_vs.h"
#include "hlsl/post_ps.h"
#include "hlsl/dl_vs.h"
#include "hlsl/dl_ps.h"
#include "hlsl/sprite_vs.h"
#include "hlsl/sprite_ps.h"
#include "hlsl/mip_start_cs.h"
#include "hlsl/mip_pass_cs.h"
#include "hlsl/mip_end_cs.h"

struct ShaderDesc
{
	const void* code;
	size_t size;
	const char* name;
};

static ShaderDesc genericPixelShaders[4] = 
{
	{ g_generic_ps, ARRAY_LEN(g_generic_ps), "generic pixel shader" },
	{ g_generic_a_ps, ARRAY_LEN(g_generic_a_ps), "generic A2C pixel shader" },
	{ g_generic_d_ps, ARRAY_LEN(g_generic_d_ps), "generic dithered pixel shader" },
	{ g_generic_ad_ps, ARRAY_LEN(g_generic_ad_ps), "generic dithered A2C pixel shader" }
};

#if defined(near)
#	undef near
#endif

#if defined(far)
#	undef far
#endif

#define MAX_GPU_TEXTURE_SIZE 2048 // instead of D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION

#define BLEND_STATE_COUNT (D3D11_BLEND_SRC_ALPHA_SAT + 1)


/*
Current info:
- feature level 10.1 minimum
- feature level 11.0 for mip-map generation with compute
- shader target 4.1 for graphics (SV_VertexID, unsized Texture2DMS)
- shader target 5.0 for compute  (typed UAVs)

To look at:
- near clip plane seems to be further in the GL2 back-end in 3D land
- find/use D3DDDIERR_DEVICEREMOVED? are the docs correct about it?
*/


enum AlphaTest
{
	AT_ALWAYS,
	AT_GREATER_THAN_0,
	AT_LESS_THAN_HALF,
	AT_GREATER_OR_EQUAL_TO_HALF
};

enum PipelineId
{
	PID_GENERIC,
	PID_SOFT_SPRITE,
	PID_DYNAMIC_LIGHT,
	PID_POST_PROCESS,
	PID_COUNT
};

enum ErrorMode
{
	EM_FATAL,
	EM_SILENT
};

enum VertexBufferId
{
	VB_POSITION,
	VB_NORMAL,
	VB_TEXCOORD,
	VB_TEXCOORD2,
	VB_COLOR,
	VB_COUNT
};

// @NOTE: MSDN says "you must set the ByteWidth value of D3D11_BUFFER_DESC in multiples of 16"
#pragma pack(push, 16)

struct GenericVSData
{
	float modelViewMatrix[16];
	float projectionMatrix[16];
	float clipPlane[4];
};

struct GenericPSData
{
	uint32_t alphaTest; // AlphaTest enum
	uint32_t texEnv; // texEnv_t enum
	float seed[2];
	float invGamma;
	float invBrightness;
	float noiseScale;
	float dummy;
};

struct SoftSpriteVSData
{
	float modelViewMatrix[16];
	float projectionMatrix[16];
	float clipPlane[4];
};

struct SoftSpritePSData
{
	uint32_t alphaTest; // AlphaTest enum
	float proj22;
	float proj32;
	float additive;
	float distance;
	float offset;
	uint32_t dummy[2];
};

struct DynamicLightVSData
{
	float modelViewMatrix[16];
	float projectionMatrix[16];
	float clipPlane[4];
	float osLightPos[4];
	float osEyePos[4];
};

struct DynamicLightPSData
{
	float lightColor[3];
	float lightRadius;
	uint32_t alphaTest; // AlphaTest enum
	uint32_t dummy[3];
};

struct PostVSData
{
	float scaleX;
	float scaleY;
	float dummy[2];
};

struct PostPSData
{
	float gamma;
	float brightness;
	float greyscale;
	float dummy;
};

struct Down4CSData
{
	float weights[4];
	uint32_t maxSize[2];
	uint32_t scale[2];
	uint32_t offset[2];
	uint32_t clampMode; // 0 = repeat
	uint32_t dummy;
};

struct LinearToGammaCSData
{
	float blendColor[4];
	float intensity;
	float invGamma;
	float dummy[2];
};

struct GammaToLinearCSData
{
	float gamma;
	float dummy[3];
};

#pragma pack(pop)

struct Texture
{
	ID3D11Texture2D* texture;
	ID3D11ShaderResourceView* view;
};

struct Pipeline
{
	ID3D11VertexShader* vertexShader;
	ID3D11PixelShader* pixelShader;
	ID3D11InputLayout* inputLayout; // can be NULL
	ID3D11Buffer* vertexBuffer; // can be NULL
	ID3D11Buffer* pixelBuffer; // can be NULL
};

struct MipGenTexture
{
	ID3D11Texture2D* texture;
	ID3D11ShaderResourceView* srv;
	ID3D11UnorderedAccessView* uav;
};

struct VertexBuffer
{
	ID3D11Buffer* buffer;
	int itemSize;
	int capacity;
	int writeIndex;
	int readIndex;
	qbool discard;
};

struct AdapterInfo
{
	qbool valid;
	int dedicatedSystemMemoryMB;
	int dedicatedVideoMemoryMB;
	int sharedSystemMemoryMB;
};

struct FrameQueries
{
	ID3D11Query* disjoint;
	ID3D11Query* frameStart;
	ID3D11Query* frameEnd;
	qbool valid;
};

struct Direct3D
{
	// constant buffer data
	PostVSData postVSData;
	PostPSData postPSData;
	float modelViewMatrix[16];
	float projectionMatrix[16];
	float clipPlane[4];
	float osLightPos[4];
	float osEyePos[4];
	float lightColor[3];
	float lightRadius;
	AlphaTest alphaTest;
	texEnv_t texEnv;
	float frameSeed[2];

	DXGI_FORMAT formatColorRT;
	DXGI_FORMAT formatDepth;     // float: DXGI_FORMAT_R32_TYPELESS
	DXGI_FORMAT formatDepthRTV;  // float: DXGI_FORMAT_R32_FLOAT
	DXGI_FORMAT formatDepthView; // float: DXGI_FORMAT_D32_FLOAT

	Texture textures[MAX_DRAWIMAGES];
	int textureCount;

	ID3D11SamplerState* samplerStates[TW_COUNT * 2];
	int samplerStateIndices[2];

	ID3D11BlendState* blendStates[2 * BLEND_STATE_COUNT * BLEND_STATE_COUNT];
	int blendStateIndex;

	ID3D11DepthStencilState* depthStencilStates[8];
	int depthStencilStateIndex;

	ID3D11RasterizerState* rasterStates[12];
	int rasterStateIndex;

	Pipeline pipelines[PID_COUNT];
	PipelineId pipelineIndex;

	MipGenTexture mipGenTextures[3]; // 0,1=float16  2=uint8

	VertexBuffer vertexBuffers[VB_COUNT];
	VertexBuffer indexBuffer;

	// for the calls to IASetVertexBuffers
	VertexBufferId vbIds[VB_COUNT];
	ID3D11Buffer* vbBuffers[VB_COUNT];
	UINT vbStrides[VB_COUNT];
	int vbCount;
	qbool splitBufferOffsets;

	ID3D11Device* device;
	ID3D11DeviceContext* context;
	IDXGISwapChain* swapChain;
	ID3D11Texture2D* backBufferTexture;
	ID3D11RenderTargetView* backBufferRTView;
	ID3D11Texture2D* renderTargetTextureMS;
	ID3D11RenderTargetView* renderTargetViewMS;
	ID3D11Texture2D* resolveTexture;
	ID3D11ShaderResourceView* resolveTextureShaderView;
	ID3D11Texture2D* depthStencilTexture;
	ID3D11DepthStencilView* depthStencilView;
	ID3D11ShaderResourceView* depthStencilShaderView;
	ID3D11Texture2D* readbackTexture; // allowed to be NULL!
	HMODULE library;

	ID3D11ComputeShader* mipGammaToLinearComputeShader;
	ID3D11ComputeShader* mipLinearToGammaComputeShader;
	ID3D11ComputeShader* mipDownSampleComputeShader;
	ID3D11Buffer* mipDownSampleConstBuffer;
	ID3D11Buffer* mipLinearToGammaConstBuffer;
	ID3D11Buffer* mipGammaToLinearConstBuffer;

	FrameQueries frameQueries[32];
	int frameQueriesWriteIndex;
	int frameQueriesReadIndex;

	// cached when starting sky rendering
	float oldSkyClipPlane[4];
	D3D11_VIEWPORT oldSkyViewport;

	ErrorMode errorMode;

	AdapterInfo adapterInfo;
};

__declspec(align(16)) static Direct3D d3d;


#define COM_RELEASE(p)			do { if(p) { p->Release(); p = NULL; } } while((void)0,0)
#define COM_RELEASE_ARRAY(a)	do { for(int i = 0; i < ARRAY_LEN(a); ++i) { COM_RELEASE(a[i]); } } while((void)0,0)


static void GAL_UpdateTexture(image_t* image, int mip, int x, int y, int w, int h, const void* data);


static const char* GetSystemErrorString(HRESULT hr)
{
	// FormatMessage might not always give us the string we want but that's ok,
	// we always print the original error code anyhow
	static char systemErrorStr[1024];
	const DWORD written = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM, NULL, (DWORD)hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		systemErrorStr, sizeof(systemErrorStr) - 1, NULL);
	if(written == 0)
	{
		Q_strncpyz(systemErrorStr, "???", sizeof(systemErrorStr));
	}

	return systemErrorStr;
}

static qbool Check(HRESULT hr, const char* function)
{
	if(SUCCEEDED(hr))
	{
		return qtrue;
	}

	if(d3d.errorMode == EM_FATAL)
	{
		ri.Error(ERR_FATAL, va("'%s' failed with code 0x%08X (%s)", function, (unsigned int)hr, GetSystemErrorString(hr)));
	}
	return qfalse;
}

static qbool CheckAndName(HRESULT hr, const char* function, ID3D11DeviceChild* resource, const char* resourceName)
{
	if(SUCCEEDED(hr))
	{
		resource->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(resourceName), resourceName);
		return qtrue;
	}

	if(d3d.errorMode == EM_FATAL)
	{
		ri.Error(ERR_FATAL, va("'%s' failed to create '%s' with code 0x%08X (%s)", function, resourceName, (unsigned int)hr, GetSystemErrorString(hr)));
	}
	return qfalse;
}

static qbool D3D11_CreateRenderTargetView(ID3D11Resource* pResource, const D3D11_RENDER_TARGET_VIEW_DESC* pDesc, ID3D11RenderTargetView** ppRTView, const char* name)
{
	const HRESULT hr = d3d.device->CreateRenderTargetView(pResource, pDesc, ppRTView);
	return CheckAndName(hr, "CreateRenderTargetView", *ppRTView, name);
}

static qbool D3D11_CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D, const char* name)
{
	const HRESULT hr = d3d.device->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
	return CheckAndName(hr, "CreateTexture2D", *ppTexture2D, name);
}

static qbool D3D11_CreateShaderResourceView(ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRView, const char* name)
{
	const HRESULT hr = d3d.device->CreateShaderResourceView(pResource, pDesc, ppSRView);
	return CheckAndName(hr, "CreateShaderResourceView", *ppSRView, name);
}

static qbool D3D11_CreateUnorderedAccessView(ID3D11Resource* pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc, ID3D11UnorderedAccessView** ppUAView, const char* name)
{
	const HRESULT hr = d3d.device->CreateUnorderedAccessView(pResource, pDesc, ppUAView);
	return CheckAndName(hr, "CreateUnorderedAccessView", *ppUAView, name);
}

static qbool D3D11_CreateVertexShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader, const char* name)
{
	const HRESULT hr = d3d.device->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
	return CheckAndName(hr, "CreateVertexShader", *ppVertexShader, name);
}

static qbool D3D11_CreatePixelShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader, const char* name)
{
	const HRESULT hr = d3d.device->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
	return CheckAndName(hr, "CreatePixelShader", *ppPixelShader, name);
}

static qbool D3D11_CreateComputeShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11ComputeShader** ppComputeShader, const char* name)
{
	const HRESULT hr = d3d.device->CreateComputeShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader);
	return CheckAndName(hr, "CreateComputeShader", *ppComputeShader, name);
}

static qbool D3D11_CreateBuffer(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer, const char* name)
{
	const HRESULT hr = d3d.device->CreateBuffer(pDesc, pInitialData, ppBuffer);
	return CheckAndName(hr, "CreateBuffer", *ppBuffer, name);
}

static qbool D3D11_CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout** ppInputLayout, const char* name)
{
	const HRESULT hr = d3d.device->CreateInputLayout(pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
	return CheckAndName(hr, "CreateInputLayout", *ppInputLayout, name);
}

static const char* GetDeviceRemovedReason()
{
	switch(d3d.device->GetDeviceRemovedReason())
	{
		case DXGI_ERROR_DEVICE_HUNG: return "device hung";
		case DXGI_ERROR_DEVICE_REMOVED: return "device removed";
		case DXGI_ERROR_DEVICE_RESET: return "device reset";
		case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "internal driver error";
		case DXGI_ERROR_INVALID_CALL: return "invalid call";
		default: return "unknown";
	}
}

static AlphaTest GetAlphaTest(unsigned int stateBits)
{
	switch(stateBits & GLS_ATEST_BITS)
	{
		case 0: return AT_ALWAYS;
		case GLS_ATEST_GT_0: return AT_GREATER_THAN_0;
		case GLS_ATEST_LT_80: return AT_LESS_THAN_HALF;
		case GLS_ATEST_GE_80: return AT_GREATER_OR_EQUAL_TO_HALF;
		default: return AT_ALWAYS;
	}
}

static D3D11_TEXTURE_ADDRESS_MODE GetTextureAddressMode(textureWrap_t wrap)
{
	switch(wrap)
	{
		case TW_CLAMP_TO_EDGE: return D3D11_TEXTURE_ADDRESS_CLAMP;
		case TW_REPEAT: return D3D11_TEXTURE_ADDRESS_WRAP;
		default: return D3D11_TEXTURE_ADDRESS_CLAMP;
	}
}

static DXGI_FORMAT GetTextureFormat(textureFormat_t f)
{
	switch(f)
	{
		case TF_RGBA8:
		default: return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}

static D3D11_CULL_MODE GetCullMode(cullType_t t)
{
	switch(t)
	{
		case CT_BACK_SIDED: return D3D11_CULL_BACK;
		case CT_FRONT_SIDED: return D3D11_CULL_FRONT;
		case CT_TWO_SIDED: return D3D11_CULL_NONE;
		default: return D3D11_CULL_NONE;
	}
}

static D3D11_BLEND GetSourceBlend(unsigned int stateBits)
{
	switch(stateBits & GLS_SRCBLEND_BITS)
	{
		case GLS_SRCBLEND_ZERO: return D3D11_BLEND_ZERO;
		case GLS_SRCBLEND_ONE: return D3D11_BLEND_ONE;
		case GLS_SRCBLEND_DST_COLOR: return D3D11_BLEND_DEST_COLOR;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR: return D3D11_BLEND_INV_DEST_COLOR;
		case GLS_SRCBLEND_SRC_ALPHA: return D3D11_BLEND_SRC_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
		case GLS_SRCBLEND_DST_ALPHA: return D3D11_BLEND_DEST_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
		case GLS_SRCBLEND_ALPHA_SATURATE: return D3D11_BLEND_SRC_ALPHA_SAT;
		default: return D3D11_BLEND_ONE;
	}
}

static D3D11_BLEND GetDestinationBlend(unsigned int stateBits)
{
	switch(stateBits & GLS_DSTBLEND_BITS)
	{
		case GLS_DSTBLEND_ZERO: return D3D11_BLEND_ZERO;
		case GLS_DSTBLEND_ONE: return D3D11_BLEND_ONE;
		case GLS_DSTBLEND_SRC_COLOR: return D3D11_BLEND_SRC_COLOR;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR: return D3D11_BLEND_INV_SRC_COLOR;
		case GLS_DSTBLEND_SRC_ALPHA: return D3D11_BLEND_SRC_ALPHA;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
		case GLS_DSTBLEND_DST_ALPHA: return D3D11_BLEND_DEST_ALPHA;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
		default: return D3D11_BLEND_ONE;
	}
}

static DXGI_FORMAT GetRenderTargetColorFormat(int format)
{
	switch(format)
	{
		case RTCF_R8G8B8A8: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case RTCF_R10G10B10A2: return DXGI_FORMAT_R10G10B10A2_UNORM;
		case RTCF_R16G16B16A16: return DXGI_FORMAT_R16G16B16A16_UNORM;
		default: return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}

static void ResetShaderData(ID3D11Resource* buffer, const void* data, size_t bytes)
{
	D3D11_MAPPED_SUBRESOURCE ms;
	HRESULT hr = d3d.context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, NULL, &ms);
	if(FAILED(hr))
	{
		ri.Error(ERR_FATAL, "Map failed");
		return;
	}
	memcpy(ms.pData, data, bytes);
	d3d.context->Unmap(buffer, NULL);
}

static void AppendVertexData(VertexBuffer* buffer, const void* data, int itemCount)
{
	D3D11_MAP mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
	if(buffer->discard || buffer->writeIndex + itemCount > buffer->capacity)
	{
		buffer->discard = qfalse;
		buffer->writeIndex = 0;
		mapType = D3D11_MAP_WRITE_DISCARD;
	}

	if(data != NULL || mapType == D3D11_MAP_WRITE_DISCARD)
	{
		D3D11_MAPPED_SUBRESOURCE ms;
		HRESULT hr = d3d.context->Map(buffer->buffer, 0, mapType, NULL, &ms);
		if(FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Map failed");
			return;
		}
		if(data != NULL)
		{
			memcpy((byte*)ms.pData + buffer->writeIndex * buffer->itemSize, data, itemCount * buffer->itemSize);
		}
		d3d.context->Unmap(buffer->buffer, NULL);
	}

	buffer->readIndex = buffer->writeIndex;
	buffer->writeIndex += itemCount;
}

static void AppendVertexDataGroup(const void* data[VB_COUNT], int vertexCount)
{
	for(int i = 0; i < VB_COUNT; ++i)
	{
		AppendVertexData(&d3d.vertexBuffers[i], data[i], vertexCount);
	}
}

static void UploadPendingShaderData()
{
	if((unsigned)d3d.pipelineIndex >= PID_COUNT)
	{
		return;
	}

	const PipelineId pid = d3d.pipelineIndex;
	Pipeline* const pipeline = &d3d.pipelines[pid];

	if(pid == PID_GENERIC)
	{
		GenericVSData vsData;
		GenericPSData psData;
		memcpy(vsData.modelViewMatrix, d3d.modelViewMatrix, sizeof(vsData.modelViewMatrix));
		memcpy(vsData.projectionMatrix, d3d.projectionMatrix, sizeof(vsData.projectionMatrix));
		memcpy(vsData.clipPlane, d3d.clipPlane, sizeof(vsData.clipPlane));
		psData.alphaTest = d3d.alphaTest;
		psData.texEnv = d3d.texEnv;
		psData.seed[0] = d3d.frameSeed[0];
		psData.seed[1] = d3d.frameSeed[1];
		psData.invGamma = 1.0f / r_gamma->value;
		psData.invBrightness = 1.0f / r_brightness->value;
		psData.noiseScale = backEnd.projection2D ? 0.0f : r_noiseScale->value;
		ResetShaderData(pipeline->vertexBuffer, &vsData, sizeof(vsData));
		ResetShaderData(pipeline->pixelBuffer, &psData, sizeof(psData));
	}
	else if(pid == PID_SOFT_SPRITE)
	{
		SoftSpriteVSData vsData;
		SoftSpritePSData psData;
		memcpy(vsData.modelViewMatrix, d3d.modelViewMatrix, sizeof(vsData.modelViewMatrix));
		memcpy(vsData.projectionMatrix, d3d.projectionMatrix, sizeof(vsData.projectionMatrix));
		memcpy(vsData.clipPlane, d3d.clipPlane, sizeof(vsData.clipPlane));
		psData.alphaTest = d3d.alphaTest;
		psData.proj22 = -vsData.projectionMatrix[2 * 4 + 2];
		psData.proj32 =  vsData.projectionMatrix[3 * 4 + 2];
		psData.additive = tess.shader->softSprite == SST_ADDITIVE ? 1.0f : 0.0f;
		psData.distance = tess.shader->softSpriteDistance;
		psData.offset = tess.shader->softSpriteOffset;
		ResetShaderData(pipeline->vertexBuffer, &vsData, sizeof(vsData));
		ResetShaderData(pipeline->pixelBuffer, &psData, sizeof(psData));
	}
	else if(pid == PID_DYNAMIC_LIGHT)
	{
		DynamicLightVSData vsData;
		DynamicLightPSData psData;
		memcpy(vsData.modelViewMatrix, d3d.modelViewMatrix, sizeof(vsData.modelViewMatrix));
		memcpy(vsData.projectionMatrix, d3d.projectionMatrix, sizeof(vsData.projectionMatrix));
		memcpy(vsData.clipPlane, d3d.clipPlane, sizeof(vsData.clipPlane));
		memcpy(vsData.osEyePos, d3d.osEyePos, sizeof(vsData.osEyePos));
		memcpy(vsData.osLightPos, d3d.osLightPos, sizeof(vsData.osLightPos));
		psData.alphaTest = d3d.alphaTest;
		memcpy(psData.lightColor, d3d.lightColor, sizeof(psData.lightColor));
		psData.lightRadius = d3d.lightRadius;
		ResetShaderData(pipeline->vertexBuffer, &vsData, sizeof(vsData));
		ResetShaderData(pipeline->pixelBuffer, &psData, sizeof(psData));
	}
	else if(pid == PID_POST_PROCESS)
	{
		ResetShaderData(pipeline->vertexBuffer, &d3d.postVSData, sizeof(d3d.postVSData));
		ResetShaderData(pipeline->pixelBuffer, &d3d.postPSData, sizeof(d3d.postPSData));
	}
}

static int ComputeSamplerStateIndex(int textureWrap, int bilinear)
{
	return bilinear * TW_COUNT + textureWrap;
}

static void ApplySamplerState(UINT slot, textureWrap_t textureWrap, qbool bilinear)
{
	const int index = ComputeSamplerStateIndex(textureWrap, bilinear);
	if(index == d3d.samplerStateIndices[slot])
	{
		return;
	}

	d3d.context->PSSetSamplers(slot, 1, &d3d.samplerStates[index]);
	d3d.samplerStateIndices[slot] = index;
}

static void DrawIndexed(int indexCount)
{
	if(d3d.splitBufferOffsets)
	{
		UINT offsets[VB_COUNT];
		for(int i = 0; i < d3d.vbCount; ++i)
		{
			VertexBuffer* const vb = &d3d.vertexBuffers[d3d.vbIds[i]];
			offsets[i] = vb->readIndex * vb->itemSize; // in bytes, not vertices
		}

		d3d.context->IASetVertexBuffers(0, d3d.vbCount, d3d.vbBuffers, d3d.vbStrides, offsets);
		d3d.context->DrawIndexed(indexCount, d3d.indexBuffer.readIndex, 0);
	}
	else
	{
		d3d.context->DrawIndexed(indexCount, d3d.indexBuffer.readIndex, d3d.vertexBuffers[VB_POSITION].readIndex);
	}
}

static void ApplyPipeline(PipelineId index)
{
	if(index == d3d.pipelineIndex || (unsigned)index >= PID_COUNT)
	{
		return;
	}

	Pipeline* const pipeline = &d3d.pipelines[index];
	if(pipeline->inputLayout)
	{
		d3d.context->IASetInputLayout(pipeline->inputLayout);

		int count = 0;
		VertexBufferId* const ids = d3d.vbIds;
		if(index == PID_GENERIC)
		{
			ids[count++] = VB_POSITION;
			ids[count++] = VB_COLOR;
			ids[count++] = VB_TEXCOORD;
			ids[count++] = VB_TEXCOORD2;
		}
		else if(index == PID_SOFT_SPRITE)
		{
			ids[count++] = VB_POSITION;
			ids[count++] = VB_COLOR;
			ids[count++] = VB_TEXCOORD;
		}
		else if(index == PID_DYNAMIC_LIGHT)
		{
			ids[count++] = VB_POSITION;
			ids[count++] = VB_NORMAL;
			ids[count++] = VB_COLOR;
			ids[count++] = VB_TEXCOORD;
		}
		d3d.vbCount = count;

		for(int i = 0; i < count; ++i)
		{
			VertexBuffer* const vb = &d3d.vertexBuffers[ids[i]];
			d3d.vbBuffers[i] = vb->buffer;
			d3d.vbStrides[i] = vb->itemSize;
		}

		if(!d3d.splitBufferOffsets)
		{
			UINT offsets[VB_COUNT] = { 0 };
			d3d.context->IASetVertexBuffers(0, count, d3d.vbBuffers, d3d.vbStrides, offsets);
		}
	}
	else
	{
		d3d.context->IASetInputLayout(NULL);
		d3d.context->IASetVertexBuffers(0, 0, NULL, NULL, NULL);
		d3d.vbCount = 0;
	}

	d3d.context->VSSetShader(pipeline->vertexShader, NULL, 0);
	d3d.context->PSSetShader(pipeline->pixelShader, NULL, 0);

	if(pipeline->vertexBuffer)
	{
		d3d.context->VSSetConstantBuffers(0, 1, &pipeline->vertexBuffer);
	}
	if(pipeline->pixelBuffer)
	{
		d3d.context->PSSetConstantBuffers(0, 1, &pipeline->pixelBuffer);
	}

	if(index == PID_POST_PROCESS)
	{
		d3d.context->OMSetRenderTargets(1, &d3d.backBufferRTView, NULL);
	}
	else if(index == PID_SOFT_SPRITE)
	{
		d3d.context->OMSetRenderTargets(1, &d3d.renderTargetViewMS, NULL);
		d3d.context->PSSetShaderResources(1, 1, &d3d.depthStencilShaderView);
		ApplySamplerState(1, TW_CLAMP_TO_EDGE, qtrue); // @TODO: nearest neighbor mode?
	}
	else
	{
		d3d.context->PSSetShaderResources(1, 1, &d3d.textures[0].view); // make sure the depth shader view isn't bound anymore
		d3d.context->OMSetRenderTargets(1, &d3d.renderTargetViewMS, d3d.depthStencilView);
	}

	d3d.pipelineIndex = index;
}

static void ApplyViewport(int x, int y, int w, int h, int th)
{
	const int top = th - y - h;

	D3D11_VIEWPORT vp;
	vp.TopLeftX = x;
	vp.TopLeftY = top;
	vp.Width = w;
	vp.Height = h;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	d3d.context->RSSetViewports(1, &vp);
}

static void ApplyScissor(int x, int y, int w, int h, int th)
{
	const int top = th - y - h;
	const int bottom = th - y;

	D3D11_RECT sr;
	sr.left = x;
	sr.top = top;
	sr.right = x + w;
	sr.bottom = bottom;
	d3d.context->RSSetScissorRects(1, &sr);
}

static void ApplyViewportAndScissor(int x, int y, int w, int h, int th)
{
	ApplyViewport(x, y, w, h, th);
	ApplyScissor(x, y, w, h, th);
}

static void CreateTexture(Texture* texture, image_t* image, int mipCount, int w, int h)
{
	COM_RELEASE(texture->texture);
	COM_RELEASE(texture->view);

	ID3D11Texture2D* texture2D;
	D3D11_TEXTURE2D_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(texDesc));
	texDesc.ArraySize = 1;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.Format = GetTextureFormat(image->format);
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.Width = w;
	texDesc.Height = h;
	texDesc.MipLevels = mipCount;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	D3D11_CreateTexture2D(&texDesc, NULL, &texture2D, image->name);

	ID3D11ShaderResourceView* view;
	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
	ZeroMemory(&viewDesc, sizeof(viewDesc));
	viewDesc.Format = texDesc.Format;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D.MipLevels = UINT(-1);
	viewDesc.Texture2D.MostDetailedMip = 0;
	D3D11_CreateShaderResourceView(texture2D, &viewDesc, &view, image->name);

	texture->texture = texture2D;
	texture->view = view;
}

static void UpdateAnimatedImage(image_t* image, int w, int h, const byte* data, qbool dirty)
{
	if(w != image->width || h != image->height)
	{
		image->width = w;
		image->height = h;
		CreateTexture(&d3d.textures[image->texnum], image, 1, w, h);
		GAL_UpdateTexture(image, 0, 0, 0, w, h, data);
	}
	else if(dirty)
	{
		GAL_UpdateTexture(image, 0, 0, 0, w, h, data);
	}
}

static const image_t* GetBundleImage(const textureBundle_t* bundle)
{
	return R_UpdateAndGetBundleImage(bundle, &UpdateAnimatedImage);
}

static int ComputeBlendStateIndex(int srcBlend, int dstBlend, qbool alphaToCoverage)
{
	return alphaToCoverage * (BLEND_STATE_COUNT * BLEND_STATE_COUNT) + (srcBlend * BLEND_STATE_COUNT) + dstBlend;
}

static void ApplyBlendState(D3D11_BLEND srcBlend, D3D11_BLEND dstBlend, qbool aphaToCoverage)
{
	const int index = ComputeBlendStateIndex(srcBlend, dstBlend, aphaToCoverage);
	if((unsigned)index >= ARRAY_LEN(d3d.blendStates))
		ri.Error(ERR_FATAL, "Tried to set an invalid blend state combo!");
	if(d3d.blendStates[index] == NULL)
		ri.Error(ERR_FATAL, "Tried to set an unregistered blend state!");

	if(index == d3d.blendStateIndex)
	{
		return;
	}

	d3d.context->OMSetBlendState(d3d.blendStates[index], NULL, 0xFFFFFFFF);
	d3d.blendStateIndex = index;
}

static int ComputeDepthStencilStateIndex(int disableDepth, int funcEqual, int maskTrue)
{
	return disableDepth | (funcEqual << 1) | (maskTrue << 2);
}

static void ApplyDepthStencilState(qbool disableDepth, qbool funcEqual, qbool maskTrue)
{
	const int index = ComputeDepthStencilStateIndex(disableDepth, funcEqual, maskTrue);
	if(index == d3d.depthStencilStateIndex)
	{
		return;
	}

	d3d.depthStencilStateIndex = index;
	d3d.context->OMSetDepthStencilState(d3d.depthStencilStates[index], 0);
}

static int ComputeRasterizerStateIndex(int wireFrame, int cullType, int polygonOffset)
{
	return cullType * 4 + wireFrame * 2 + polygonOffset;
}

static void ApplyRasterizerState(qbool wireFrame, cullType_t cullType, qbool polygonOffset)
{
	const int index = ComputeRasterizerStateIndex(wireFrame, cullType, polygonOffset);
	if(index == d3d.rasterStateIndex)
	{
		return;
	}

	d3d.rasterStateIndex = index;
	d3d.context->RSSetState(d3d.rasterStates[index]);
}

static void ApplyState(unsigned int stateBits, cullType_t cullType, qbool polygonOffset)
{
	static unsigned int oldStateBits = 0;

	const unsigned int diffBits = oldStateBits ^ stateBits;
	oldStateBits = stateBits;

	d3d.alphaTest = GetAlphaTest(stateBits);

	if(diffBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_ATEST_BITS))
	{
		const D3D11_BLEND srcBlend = (stateBits & GLS_SRCBLEND_BITS) ? GetSourceBlend(stateBits) : D3D11_BLEND_ONE;
		const D3D11_BLEND dstBlend = (stateBits & GLS_DSTBLEND_BITS) ? GetDestinationBlend(stateBits) : D3D11_BLEND_ZERO;
		ApplyBlendState(srcBlend, dstBlend, glInfo.alphaToCoverageSupport && d3d.pipelineIndex == PID_GENERIC && d3d.alphaTest != AT_ALWAYS);
	}

	const qbool disableDepth = (stateBits & GLS_DEPTHTEST_DISABLE) ? 1 : 0;
	const qbool funcEqual = (stateBits & GLS_DEPTHFUNC_EQUAL) ? 1 : 0;
	const qbool maskTrue = (stateBits & GLS_DEPTHMASK_TRUE) ? 1 : 0;
	ApplyDepthStencilState(disableDepth, funcEqual, maskTrue);

	// fix up the cull mode for mirrors
	if(backEnd.viewParms.isMirror)
	{
		if(cullType == CT_BACK_SIDED)
		{
			cullType = CT_FRONT_SIDED;
		}
		else if(cullType == CT_FRONT_SIDED)
		{
			cullType = CT_BACK_SIDED;
		}
	}
	ApplyRasterizerState((stateBits & GLS_POLYMODE_LINE) != 0, cullType, polygonOffset);
}

static void BindImage(UINT slot, const image_t* image)
{
	ID3D11ShaderResourceView* view = d3d.textures[image->texnum].view;
	d3d.context->PSSetShaderResources(slot, 1, &view);
	ApplySamplerState(slot, image->wrapClampMode, (image->flags & IMG_NOAF) != 0);
}

static void BindBundle(UINT slot, const textureBundle_t* bundle)
{
	BindImage(slot, GetBundleImage(bundle));
}

static void FindBestAvailableAA(DXGI_SAMPLE_DESC* sampleDesc)
{
	// @NOTE: D3D10_MAX_MULTISAMPLE_SAMPLE_COUNT == D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT
	sampleDesc->Count = (UINT)min(r_msaa->integer, D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT);
	sampleDesc->Quality = 0;

	while(sampleDesc->Count > 0)
	{
		UINT levelCount = 0;
		if(SUCCEEDED(d3d.device->CheckMultisampleQualityLevels(d3d.formatColorRT, sampleDesc->Count, &levelCount)) &&
		   levelCount > 0 &&
		   SUCCEEDED(d3d.device->CheckMultisampleQualityLevels(d3d.formatDepth, sampleDesc->Count, &levelCount)) &&
		   levelCount > 0)
		   break;

		--sampleDesc->Count;
	}

	if(sampleDesc->Count <= 1)
	{
		sampleDesc->Count = 1;
		sampleDesc->Quality = 0;
	}
}

static qbool GAL_Init()
{
	Sys_V_Init(GAL_D3D11);

	ZeroMemory(&d3d, sizeof(d3d));

	d3d.library = LoadLibraryA("D3D11.dll");
	if(d3d.library == NULL)
		ri.Error(ERR_FATAL, "D3D11.dll couldn't be found or opened");

	PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN pD3D11CreateDeviceAndSwapChain =
		(PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)GetProcAddress(d3d.library, "D3D11CreateDeviceAndSwapChain");
	if(pD3D11CreateDeviceAndSwapChain == NULL)
		ri.Error(ERR_FATAL, "Failed to locate D3D11CreateDeviceAndSwapChain in D3D11.dll");

	const D3D_FEATURE_LEVEL featureLevels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
	UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#if defined(_DEBUG)
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	swapChainDesc.BufferCount = 1;
	swapChainDesc.BufferDesc.Width = glInfo.winWidth;
	swapChainDesc.BufferDesc.Height = glInfo.winHeight;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = GetActiveWindow();
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = 0;

create_device:
	HRESULT hr = (*pD3D11CreateDeviceAndSwapChain)(
		NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, featureLevels, ARRAY_LEN(featureLevels), D3D11_SDK_VERSION,
		&swapChainDesc, &d3d.swapChain, &d3d.device, NULL, &d3d.context);
	if(hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
	{
		ri.Printf(PRINT_WARNING, "D3D11CreateDeviceAndSwapChain failed because you don't have the SDK installed.\n");
		ri.Printf(PRINT_WARNING, "Trying to create the device again without the debug layer...\n");
		flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		goto create_device;
	}
	Check(hr, "D3D11CreateDeviceAndSwapChain");

	d3d.formatColorRT = GetRenderTargetColorFormat(r_rtColorFormat->integer);
	d3d.formatDepth = DXGI_FORMAT_R24G8_TYPELESS;
	d3d.formatDepthRTV = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	d3d.formatDepthView = DXGI_FORMAT_D24_UNORM_S8_UINT;

	D3D11_TEXTURE2D_DESC readbackTexDesc;
	ZeroMemory(&readbackTexDesc, sizeof(readbackTexDesc));
	readbackTexDesc.Width = glConfig.vidWidth;
	readbackTexDesc.Height = glConfig.vidHeight;
	readbackTexDesc.MipLevels = 1;
	readbackTexDesc.ArraySize = 1;
	readbackTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	readbackTexDesc.SampleDesc.Count = 1;
	readbackTexDesc.SampleDesc.Quality = 0;
	readbackTexDesc.Usage = D3D11_USAGE_STAGING;
	readbackTexDesc.BindFlags = 0;
	readbackTexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	readbackTexDesc.MiscFlags = 0;
	d3d.errorMode = EM_SILENT;
	if(!D3D11_CreateTexture2D(&readbackTexDesc, 0, &d3d.readbackTexture, "screenshot/video readback texture"))
		ri.Printf(PRINT_WARNING, "Screengrab texture creation failed! /" S_COLOR_CMD "screenshot^7* and /" S_COLOR_CMD "video^7 won't work\n");
	d3d.errorMode = EM_FATAL;

	hr = d3d.swapChain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&d3d.backBufferTexture);
	CheckAndName(hr, "GetBuffer", d3d.backBufferTexture, "back buffer texture");

	D3D11_RENDER_TARGET_VIEW_DESC colorViewDesc; // needed?
	ZeroMemory(&colorViewDesc, sizeof(colorViewDesc));
	colorViewDesc.Format = swapChainDesc.BufferDesc.Format;
	colorViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	colorViewDesc.Texture2D.MipSlice = 0;
	D3D11_CreateRenderTargetView(d3d.backBufferTexture, &colorViewDesc, &d3d.backBufferRTView, "back buffer render target view");

	DXGI_SAMPLE_DESC sampleDesc;
	FindBestAvailableAA(&sampleDesc);
	const qbool alphaToCoverageOK = sampleDesc.Count > 1 && r_alphaToCoverage->integer != 0;

	D3D11_TEXTURE2D_DESC renderTargetTexDesc;
	ZeroMemory(&renderTargetTexDesc, sizeof(renderTargetTexDesc));
	renderTargetTexDesc.Width = glConfig.vidWidth;
	renderTargetTexDesc.Height = glConfig.vidHeight;
	renderTargetTexDesc.MipLevels = 1;
	renderTargetTexDesc.ArraySize = 1;
	renderTargetTexDesc.Format = d3d.formatColorRT;
	renderTargetTexDesc.SampleDesc.Count = sampleDesc.Count;
	renderTargetTexDesc.SampleDesc.Quality = sampleDesc.Quality;
	renderTargetTexDesc.Usage = D3D11_USAGE_DEFAULT;
	renderTargetTexDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
	renderTargetTexDesc.CPUAccessFlags = 0;
	renderTargetTexDesc.MiscFlags = 0;
	D3D11_CreateTexture2D(&renderTargetTexDesc, 0, &d3d.renderTargetTextureMS, "MS render target texture");

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
	ZeroMemory(&rtvDesc, sizeof(rtvDesc));
	rtvDesc.Format = renderTargetTexDesc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
	D3D11_CreateRenderTargetView(d3d.renderTargetTextureMS, &rtvDesc, &d3d.renderTargetViewMS, "MS render target view");

	ZeroMemory(&renderTargetTexDesc, sizeof(renderTargetTexDesc));
	renderTargetTexDesc.Width = glConfig.vidWidth;
	renderTargetTexDesc.Height = glConfig.vidHeight;
	renderTargetTexDesc.MipLevels = 1;
	renderTargetTexDesc.ArraySize = 1;
	renderTargetTexDesc.Format = d3d.formatColorRT;
	renderTargetTexDesc.SampleDesc.Count = 1;
	renderTargetTexDesc.SampleDesc.Quality = 0;
	renderTargetTexDesc.Usage = D3D11_USAGE_DEFAULT;
	renderTargetTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	renderTargetTexDesc.CPUAccessFlags = 0;
	renderTargetTexDesc.MiscFlags = 0;
	D3D11_CreateTexture2D(&renderTargetTexDesc, 0, &d3d.resolveTexture, "resolve texture");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = renderTargetTexDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	D3D11_CreateShaderResourceView(d3d.resolveTexture, &srvDesc, &d3d.resolveTextureShaderView, "resolve texture shader resource view");

	D3D11_TEXTURE2D_DESC depthStencilTexDesc;
	ZeroMemory(&depthStencilTexDesc, sizeof(depthStencilTexDesc));
	depthStencilTexDesc.Width = glConfig.vidWidth;
	depthStencilTexDesc.Height = glConfig.vidHeight;
	depthStencilTexDesc.MipLevels = 1;
	depthStencilTexDesc.ArraySize = 1;
	depthStencilTexDesc.Format = d3d.formatDepth;
	depthStencilTexDesc.SampleDesc.Count = sampleDesc.Count;
	depthStencilTexDesc.SampleDesc.Quality = sampleDesc.Quality;
	depthStencilTexDesc.Usage = D3D11_USAGE_DEFAULT;
	depthStencilTexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	depthStencilTexDesc.CPUAccessFlags = 0;
	depthStencilTexDesc.MiscFlags = 0;
	D3D11_CreateTexture2D(&depthStencilTexDesc, 0, &d3d.depthStencilTexture, "depth stencil texture");

	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
	depthStencilViewDesc.Format = d3d.formatDepthView;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
	depthStencilViewDesc.Texture2D.MipSlice = 0;
	hr = d3d.device->CreateDepthStencilView(d3d.depthStencilTexture, &depthStencilViewDesc, &d3d.depthStencilView);
	CheckAndName(hr, "CreateDepthStencilView", d3d.depthStencilView, "depth stencil view");

	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = d3d.formatDepthRTV;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
	D3D11_CreateShaderResourceView(d3d.depthStencilTexture, &srvDesc, &d3d.depthStencilShaderView, "depth stencil shader resource view");

	const ShaderDesc* const genericPS = &genericPixelShaders[(alphaToCoverageOK != 0) + 2 * (r_dither->integer != 0)];
	D3D11_CreateVertexShader(g_generic_vs, ARRAY_LEN(g_generic_vs), NULL, &d3d.pipelines[PID_GENERIC].vertexShader, "generic vertex shader");
	D3D11_CreatePixelShader(genericPS->code, genericPS->size, NULL, &d3d.pipelines[PID_GENERIC].pixelShader, genericPS->name);

	D3D11_INPUT_ELEMENT_DESC genericInputLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 3, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	D3D11_CreateInputLayout(genericInputLayoutDesc, ARRAY_LEN(genericInputLayoutDesc), g_generic_vs, ARRAY_LEN(g_generic_vs), &d3d.pipelines[PID_GENERIC].inputLayout, "generic input layout");

	d3d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const int maxVertexCount = SHADER_MAX_VERTEXES;
	const int maxIndexCount = SHADER_MAX_INDEXES;

	VertexBuffer* const vb = d3d.vertexBuffers;
	vb[VB_POSITION].itemSize = sizeof(vec4_t);
	vb[VB_NORMAL].itemSize = sizeof(vec4_t);
	vb[VB_TEXCOORD].itemSize = sizeof(vec2_t);
	vb[VB_TEXCOORD2].itemSize = sizeof(vec2_t);
	vb[VB_COLOR].itemSize = sizeof(color4ub_t);
	d3d.indexBuffer.itemSize = sizeof(uint32_t);
	for(int i = 0; i < ARRAY_LEN(d3d.vertexBuffers); ++i)
	{
		vb[i].capacity = maxVertexCount;
		vb[i].discard = qtrue;
	}
	d3d.indexBuffer.capacity = maxIndexCount;
	d3d.indexBuffer.discard = qtrue;

	D3D11_BUFFER_DESC vertexBufferDesc;
	ZeroMemory(&vertexBufferDesc, sizeof(vertexBufferDesc));
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = maxVertexCount * vb[VB_POSITION].itemSize;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&vertexBufferDesc, NULL, &vb[VB_POSITION].buffer, "position vertex buffer");
	D3D11_CreateBuffer(&vertexBufferDesc, NULL, &vb[VB_NORMAL].buffer, "normal vertex buffer");

	D3D11_BUFFER_DESC colorBufferDesc;
	ZeroMemory(&colorBufferDesc, sizeof(colorBufferDesc));
	colorBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	colorBufferDesc.ByteWidth = maxVertexCount * vb[VB_COLOR].itemSize;
	colorBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	colorBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&colorBufferDesc, NULL, &vb[VB_COLOR].buffer, "color vertex buffer");

	D3D11_BUFFER_DESC texCoordBufferDesc;
	ZeroMemory(&texCoordBufferDesc, sizeof(texCoordBufferDesc));
	texCoordBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	texCoordBufferDesc.ByteWidth = maxVertexCount * vb[VB_TEXCOORD].itemSize;
	texCoordBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	texCoordBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&texCoordBufferDesc, NULL, &vb[VB_TEXCOORD].buffer, "texture coordinates vertex buffer #1");
	D3D11_CreateBuffer(&texCoordBufferDesc, NULL, &vb[VB_TEXCOORD2].buffer, "texture coordinates vertex buffer #2");

	D3D11_BUFFER_DESC indexBufferDesc;
	ZeroMemory(&indexBufferDesc, sizeof(indexBufferDesc));
	indexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	indexBufferDesc.ByteWidth = maxIndexCount * d3d.indexBuffer.itemSize;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&indexBufferDesc, NULL, &d3d.indexBuffer.buffer, "index buffer");

	d3d.context->IASetIndexBuffer(d3d.indexBuffer.buffer, DXGI_FORMAT_R32_UINT, 0);

	D3D11_BUFFER_DESC vertexShaderBufferDesc;
	ZeroMemory(&vertexShaderBufferDesc, sizeof(vertexShaderBufferDesc));
	vertexShaderBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexShaderBufferDesc.ByteWidth = sizeof(GenericVSData);
	vertexShaderBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	vertexShaderBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&vertexShaderBufferDesc, NULL, &d3d.pipelines[PID_GENERIC].vertexBuffer, "generic vertex shader buffer");

	D3D11_BUFFER_DESC pixelShaderBufferDesc;
	ZeroMemory(&pixelShaderBufferDesc, sizeof(pixelShaderBufferDesc));
	pixelShaderBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	pixelShaderBufferDesc.ByteWidth = sizeof(GenericPSData);
	pixelShaderBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	pixelShaderBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&pixelShaderBufferDesc, NULL, &d3d.pipelines[PID_GENERIC].pixelBuffer, "generic pixel shader buffer");

	// create all sampler states
	for(int bilinear = 0; bilinear < 2; ++bilinear)
	{
		for(int wrapMode = 0; wrapMode < TW_COUNT; ++wrapMode)
		{
			const int index = ComputeSamplerStateIndex(wrapMode, bilinear);

			// @NOTE: D3D10_REQ_MAXANISOTROPY == D3D11_REQ_MAXANISOTROPY
			const int maxAnisotropy = Com_ClampInt(1, D3D11_REQ_MAXANISOTROPY, r_ext_max_anisotropy->integer);
			const D3D11_TEXTURE_ADDRESS_MODE mode = GetTextureAddressMode((textureWrap_t)wrapMode);
			ID3D11SamplerState* samplerState;
			D3D11_SAMPLER_DESC samplerDesc;
			ZeroMemory(&samplerDesc, sizeof(samplerDesc));
			samplerDesc.Filter = (bilinear || maxAnisotropy == 1) ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_ANISOTROPIC;
			samplerDesc.AddressU = mode;
			samplerDesc.AddressV = mode;
			samplerDesc.AddressW = mode;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			samplerDesc.MinLOD = -D3D11_FLOAT32_MAX;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			samplerDesc.MaxAnisotropy = bilinear ? 1 : maxAnisotropy;
			hr = d3d.device->CreateSamplerState(&samplerDesc, &samplerState);
			CheckAndName(hr, "CreateSamplerState", samplerState, va("sampler state %d", index));

			d3d.samplerStates[index] = samplerState;
		}
	}

	// force set the default sampler states
	for(int i = 0; i < ARRAY_LEN(d3d.samplerStateIndices); ++i)
	{
		d3d.samplerStateIndices[i] = -1;
		ApplySamplerState(i, TW_CLAMP_TO_EDGE, qtrue);
	}

	// create all blend states
	const int coverageModes = alphaToCoverageOK ? 2 : 1;
	for(int a = 0; a < coverageModes; ++a)
	{
		for(int s = 1; s < BLEND_STATE_COUNT; ++s)
		{
			for(int d = 1; d < BLEND_STATE_COUNT; ++d)
			{
				const int index = ComputeBlendStateIndex(s, d, a);

				ID3D11BlendState* blendState;
				D3D11_BLEND_DESC blendDesc;
				ZeroMemory(&blendDesc, sizeof(blendDesc));
				blendDesc.AlphaToCoverageEnable = a == 1 ? TRUE : FALSE;
				blendDesc.RenderTarget[0].BlendEnable = TRUE;
				blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
				blendDesc.RenderTarget[0].SrcBlend = (D3D11_BLEND)s;
				blendDesc.RenderTarget[0].DestBlend = (D3D11_BLEND)d;
				blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
				blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
				blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
				hr = d3d.device->CreateBlendState(&blendDesc, &blendState);
				CheckAndName(hr, "CreateBlendState", blendState, va("blend state %d", index));

				d3d.blendStates[index] = blendState;
			}
		}
	}

	// create all the depth/stencil states
	for(int disableDepth = 0; disableDepth < 2; ++disableDepth)
	{
		for(int funcEqual = 0; funcEqual < 2; ++funcEqual)
		{
			for(int maskTrue = 0; maskTrue < 2; ++maskTrue)
			{
				const int index = ComputeDepthStencilStateIndex(disableDepth, funcEqual, maskTrue);

				ID3D11DepthStencilState* depthState;
				D3D11_DEPTH_STENCIL_DESC depthDesc;
				ZeroMemory(&depthDesc, sizeof(depthDesc));
				depthDesc.DepthEnable = disableDepth ? FALSE : TRUE;
				depthDesc.DepthFunc = funcEqual ? D3D11_COMPARISON_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
				depthDesc.DepthWriteMask = maskTrue ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
				depthDesc.StencilEnable = FALSE;
				hr = d3d.device->CreateDepthStencilState(&depthDesc, &depthState);
				CheckAndName(hr, "CreateDepthStencilState", depthState, va("depth/stencil state %d", index));
				
				d3d.depthStencilStates[index] = depthState;
			}
		}
	}

	// create all the raster states
	for(int polygonOffset = 0; polygonOffset < 2; ++polygonOffset)
	{
		for(int wireFrame = 0; wireFrame < 2; ++wireFrame)
		{
			for(int cullType = 0; cullType < CT_COUNT; ++cullType)
			{
				const int index = ComputeRasterizerStateIndex(wireFrame, cullType, polygonOffset);

				ID3D11RasterizerState* rasterState;
				D3D11_RASTERIZER_DESC rasterDesc;
				ZeroMemory(&rasterDesc, sizeof(rasterDesc));
				rasterDesc.FillMode = wireFrame ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
				rasterDesc.CullMode = GetCullMode((cullType_t)cullType);
				rasterDesc.FrontCounterClockwise = TRUE;
				rasterDesc.ScissorEnable = TRUE;
				rasterDesc.DepthClipEnable = FALSE;
				rasterDesc.DepthBiasClamp = 0.0f;
				rasterDesc.DepthBias = polygonOffset ? -1 : 0;
				rasterDesc.SlopeScaledDepthBias = polygonOffset ? -1.0f : 0.0f;
				hr = d3d.device->CreateRasterizerState(&rasterDesc, &rasterState);
				CheckAndName(hr, "CreateRasterizerState", rasterState, va("raster state %d", index));

				d3d.rasterStates[index] = rasterState;
			}
		}
	}

	//
	// post-processing
	//

	D3D11_CreateVertexShader(g_post_vs, ARRAY_LEN(g_post_vs), NULL, &d3d.pipelines[PID_POST_PROCESS].vertexShader, "post-process vertex shader");
	D3D11_CreatePixelShader(g_post_ps, ARRAY_LEN(g_post_ps), NULL, &d3d.pipelines[PID_POST_PROCESS].pixelShader, "post-process pixel shader");

	ZeroMemory(&vertexShaderBufferDesc, sizeof(vertexShaderBufferDesc));
	vertexShaderBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexShaderBufferDesc.ByteWidth = sizeof(d3d.postVSData);
	vertexShaderBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	vertexShaderBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&vertexShaderBufferDesc, NULL, &d3d.pipelines[PID_POST_PROCESS].vertexBuffer, "post-process vertex shader buffer");

	ZeroMemory(&pixelShaderBufferDesc, sizeof(pixelShaderBufferDesc));
	pixelShaderBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	pixelShaderBufferDesc.ByteWidth = sizeof(d3d.postPSData);
	pixelShaderBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	pixelShaderBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&pixelShaderBufferDesc, NULL, &d3d.pipelines[PID_POST_PROCESS].pixelBuffer, "post-process pixel shader buffer");

	//
	// dynamic lights
	//

	D3D11_CreateVertexShader(g_dl_vs, ARRAY_LEN(g_dl_vs), NULL, &d3d.pipelines[PID_DYNAMIC_LIGHT].vertexShader, "dynamic light vertex shader");
	D3D11_CreatePixelShader(g_dl_ps, ARRAY_LEN(g_dl_ps), NULL, &d3d.pipelines[PID_DYNAMIC_LIGHT].pixelShader, "dynamic light pixel shader");

	D3D11_INPUT_ELEMENT_DESC dlInputLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 2, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 3, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	D3D11_CreateInputLayout(dlInputLayoutDesc, ARRAY_LEN(dlInputLayoutDesc), g_dl_vs, ARRAY_LEN(g_dl_vs), &d3d.pipelines[PID_DYNAMIC_LIGHT].inputLayout, "dynamic light input layout");

	ZeroMemory(&vertexShaderBufferDesc, sizeof(vertexShaderBufferDesc));
	vertexShaderBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexShaderBufferDesc.ByteWidth = sizeof(DynamicLightVSData);
	vertexShaderBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	vertexShaderBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&vertexShaderBufferDesc, NULL, &d3d.pipelines[PID_DYNAMIC_LIGHT].vertexBuffer, "dynamic light vertex shader buffer");

	ZeroMemory(&pixelShaderBufferDesc, sizeof(pixelShaderBufferDesc));
	pixelShaderBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	pixelShaderBufferDesc.ByteWidth = sizeof(DynamicLightPSData);
	pixelShaderBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	pixelShaderBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&pixelShaderBufferDesc, NULL, &d3d.pipelines[PID_DYNAMIC_LIGHT].pixelBuffer, "dynamic light pixel shader buffer");

	//
	// soft sprites
	//

	D3D11_CreateVertexShader(g_sprite_vs, ARRAY_LEN(g_sprite_vs), NULL, &d3d.pipelines[PID_SOFT_SPRITE].vertexShader, "soft sprite vertex shader");
	D3D11_CreatePixelShader(g_sprite_ps, ARRAY_LEN(g_sprite_ps), NULL, &d3d.pipelines[PID_SOFT_SPRITE].pixelShader, "soft sprite pixel shader");
	
	D3D11_INPUT_ELEMENT_DESC ssInputLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	D3D11_CreateInputLayout(ssInputLayoutDesc, ARRAY_LEN(ssInputLayoutDesc), g_sprite_vs, ARRAY_LEN(g_sprite_vs), &d3d.pipelines[PID_SOFT_SPRITE].inputLayout, "soft sprite input layout");
	
	ZeroMemory(&vertexShaderBufferDesc, sizeof(vertexShaderBufferDesc));
	vertexShaderBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexShaderBufferDesc.ByteWidth = sizeof(SoftSpriteVSData);
	vertexShaderBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	vertexShaderBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&vertexShaderBufferDesc, NULL, &d3d.pipelines[PID_SOFT_SPRITE].vertexBuffer, "soft sprite vertex shader buffer");

	ZeroMemory(&pixelShaderBufferDesc, sizeof(pixelShaderBufferDesc));
	pixelShaderBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	pixelShaderBufferDesc.ByteWidth = sizeof(SoftSpritePSData);
	pixelShaderBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	pixelShaderBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_CreateBuffer(&pixelShaderBufferDesc, NULL, &d3d.pipelines[PID_SOFT_SPRITE].pixelBuffer, "soft sprite pixel shader buffer");

	//
	// mip-map generation
	//

	qbool mipGenOK = qfalse;
	if(r_gpuMipGen->integer && d3d.device->GetFeatureLevel() == D3D_FEATURE_LEVEL_11_0)
	{
		d3d.errorMode = EM_SILENT;

		mipGenOK = qtrue;
		mipGenOK &= D3D11_CreateComputeShader(g_mip_pass_cs, ARRAY_LEN(g_mip_pass_cs), NULL, &d3d.mipDownSampleComputeShader, "mip-map down-sampling compute shader");
		mipGenOK &= D3D11_CreateComputeShader(g_mip_start_cs, ARRAY_LEN(g_mip_start_cs), NULL, &d3d.mipGammaToLinearComputeShader, "gamma-to-linear compute shader");
		mipGenOK &= D3D11_CreateComputeShader(g_mip_end_cs, ARRAY_LEN(g_mip_end_cs), NULL, &d3d.mipLinearToGammaComputeShader, "linear-to-gamma compute shader");

		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.ByteWidth = sizeof(Down4CSData);
		mipGenOK &= D3D11_CreateBuffer(&bufferDesc, NULL, &d3d.mipDownSampleConstBuffer, "mip-map down-sampling compute shader buffer");
		bufferDesc.ByteWidth = sizeof(LinearToGammaCSData);
		mipGenOK &= D3D11_CreateBuffer(&bufferDesc, NULL, &d3d.mipLinearToGammaConstBuffer, "mip-map linear-to-gamma compute shader buffer");
		bufferDesc.ByteWidth = sizeof(GammaToLinearCSData);
		mipGenOK &= D3D11_CreateBuffer(&bufferDesc, NULL, &d3d.mipGammaToLinearConstBuffer, "mip-map gamma-to-linear compute shader buffer");

		for(int i = 0; i < ARRAY_LEN(d3d.mipGenTextures); ++i)
		{
			D3D11_TEXTURE2D_DESC textureDesc;
			ZeroMemory(&textureDesc, sizeof(textureDesc));
			textureDesc.Width = MAX_GPU_TEXTURE_SIZE;
			textureDesc.Height = MAX_GPU_TEXTURE_SIZE;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = i == 2 ? DXGI_FORMAT_R8G8B8A8_UINT : DXGI_FORMAT_R16G16B16A16_FLOAT;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			textureDesc.CPUAccessFlags = 0;
			textureDesc.MiscFlags = 0;
			mipGenOK &= D3D11_CreateTexture2D(&textureDesc, 0, &d3d.mipGenTextures[i].texture, va("mip-map generation texture #%d", i + 1));

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			ZeroMemory(&srvDesc, sizeof(srvDesc));
			srvDesc.Format = textureDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.MostDetailedMip = 0;
			mipGenOK &= D3D11_CreateShaderResourceView(d3d.mipGenTextures[i].texture, &srvDesc, &d3d.mipGenTextures[i].srv, va("mip-map generation SRV #%d", i + 1));

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
			ZeroMemory(&uavDesc, sizeof(uavDesc));
			uavDesc.Format = textureDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			mipGenOK &= D3D11_CreateUnorderedAccessView(d3d.mipGenTextures[i].texture, &uavDesc, &d3d.mipGenTextures[i].uav, va("mip-map generation SRV #%d", i + 1));
		}

		d3d.errorMode = EM_FATAL;
	}
	
	//
	// misc.
	//

	// select the generic pipeline to begin with
	d3d.pipelineIndex = (PipelineId)-1;
	ApplyPipeline(PID_GENERIC);

	// force set all the default non-sampler states
	d3d.blendStateIndex = -1;
	d3d.depthStencilStateIndex = -1;
	d3d.rasterStateIndex = -1;
	ApplyState(GLS_DEFAULT, CT_TWO_SIDED, qfalse);

	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;
	glConfig.unused_maxTextureSize = MAX_GPU_TEXTURE_SIZE;
	glConfig.unused_maxActiveTextures = 0;
	glConfig.unused_driverType = 0;		// ICD
	glConfig.unused_hardwareType = 0;	// generic
	glConfig.unused_deviceSupportsGamma = qtrue;
	glConfig.unused_textureCompression = 0;	// no compression
	glConfig.unused_textureEnvAddAvailable = qtrue;
	glConfig.unused_displayFrequency = 0;
	glConfig.unused_isFullscreen = !!r_fullscreen->integer;
	glConfig.unused_stereoEnabled = qfalse;
	glConfig.unused_smpActive = qfalse;
	glConfig.extensions_string[0] = '\0';
	glConfig.renderer_string[0] = '\0';
	glConfig.vendor_string[0] = '\0';
	glConfig.version_string[0] = '\0';
	glInfo.displayFrequency = 0;
	glInfo.maxAnisotropy = D3D11_REQ_MAXANISOTROPY;	// @NOTE: D3D10_REQ_MAXANISOTROPY == D3D11_REQ_MAXANISOTROPY
	glInfo.maxTextureSize = MAX_GPU_TEXTURE_SIZE;
	glInfo.softSpriteSupport = qtrue;
	glInfo.mipGenSupport = mipGenOK;
	glInfo.alphaToCoverageSupport = alphaToCoverageOK;

	IDXGIDevice* dxgiDevice;
	if(SUCCEEDED(d3d.device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice)))
	{
		IDXGIAdapter* dxgiAdapter;
		if(SUCCEEDED(dxgiDevice->GetAdapter(&dxgiAdapter)))
		{
			DXGI_ADAPTER_DESC desc;
			if(SUCCEEDED(dxgiAdapter->GetDesc(&desc)))
			{
				char name[ARRAY_LEN(desc.Description) + 1];
				if(WideCharToMultiByte(CP_UTF7, 0, desc.Description, -1, name, sizeof(name) - 1, NULL, NULL) > 0)
				{
					Q_strncpyz(glConfig.renderer_string, name, sizeof(glConfig.renderer_string));
				}

				d3d.adapterInfo.valid = qtrue;
				d3d.adapterInfo.dedicatedSystemMemoryMB = (int)(desc.DedicatedSystemMemory >> 20);
				d3d.adapterInfo.dedicatedVideoMemoryMB = (int)(desc.DedicatedVideoMemory >> 20);
				d3d.adapterInfo.sharedSystemMemoryMB = (int)(desc.SharedSystemMemory >> 20);
			}
		}

		COM_RELEASE(dxgiDevice);
	}

	qbool maxFrameLatencySet = qfalse;
	IDXGIDevice1* dxgiDevice1;
	if(SUCCEEDED(d3d.device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice1)))
	{
		if(SUCCEEDED(dxgiDevice1->SetMaximumFrameLatency(r_d3d11_maxQueuedFrames->integer)))
		{
			maxFrameLatencySet = qtrue;
		}		

		COM_RELEASE(dxgiDevice1);
	}
	if(maxFrameLatencySet)
	{
		ri.Printf(PRINT_ALL, "Max. queued frames: %d\n", r_d3d11_maxQueuedFrames->integer);
	}
	else
	{
		ri.Printf(PRINT_ERROR, "Failed to set the max. number of queued frames\n");
	}

	if(r_d3d11_syncOffsets->integer == D3D11SO_AUTO)
	{
		// only nVidia's drivers seem to consistently handle the extra IASetVertexBuffers calls well enough
		d3d.splitBufferOffsets = Q_stristr(glConfig.renderer_string, "NVIDIA") != NULL;
	}
	else
	{
		d3d.splitBufferOffsets = r_d3d11_syncOffsets->integer == D3D11SO_SPLITOFFSETS;
	}

	ri.Printf(PRINT_ALL, "MSAA: %d samples requested, %d selected\n", r_msaa->integer, sampleDesc.Count);

	return qtrue;
}

static void GAL_ShutDown(qbool fullShutDown)
{
	for(int i = 0; i < d3d.textureCount; ++i)
	{
		COM_RELEASE(d3d.textures[i].view);
		COM_RELEASE(d3d.textures[i].texture);
	}

	for(int i = 0; i < ARRAY_LEN(d3d.pipelines); ++i)
	{
		COM_RELEASE(d3d.pipelines[i].inputLayout);
		COM_RELEASE(d3d.pipelines[i].vertexShader);
		COM_RELEASE(d3d.pipelines[i].pixelShader);
		COM_RELEASE(d3d.pipelines[i].vertexBuffer);
		COM_RELEASE(d3d.pipelines[i].pixelBuffer);
	}

	for(int i = 0; i < ARRAY_LEN(d3d.mipGenTextures); ++i)
	{
		COM_RELEASE(d3d.mipGenTextures[i].texture);
		COM_RELEASE(d3d.mipGenTextures[i].srv);
		COM_RELEASE(d3d.mipGenTextures[i].uav);
	}

	for(int i = 0; i < ARRAY_LEN(d3d.vertexBuffers); ++i)
	{
		COM_RELEASE(d3d.vertexBuffers[i].buffer);
	}
	COM_RELEASE(d3d.indexBuffer.buffer);

	COM_RELEASE_ARRAY(d3d.samplerStates);
	COM_RELEASE_ARRAY(d3d.blendStates);
	COM_RELEASE_ARRAY(d3d.depthStencilStates);
	COM_RELEASE_ARRAY(d3d.rasterStates);

	COM_RELEASE(d3d.backBufferTexture);
	COM_RELEASE(d3d.backBufferRTView);
	COM_RELEASE(d3d.renderTargetTextureMS);
	COM_RELEASE(d3d.renderTargetViewMS);
	COM_RELEASE(d3d.resolveTexture);
	COM_RELEASE(d3d.resolveTextureShaderView);
	COM_RELEASE(d3d.depthStencilTexture);
	COM_RELEASE(d3d.depthStencilView);
	COM_RELEASE(d3d.depthStencilShaderView);
	COM_RELEASE(d3d.readbackTexture);
	COM_RELEASE(d3d.mipGammaToLinearComputeShader);
	COM_RELEASE(d3d.mipLinearToGammaComputeShader);
	COM_RELEASE(d3d.mipDownSampleComputeShader);
	COM_RELEASE(d3d.mipDownSampleConstBuffer);
	COM_RELEASE(d3d.mipLinearToGammaConstBuffer);
	COM_RELEASE(d3d.mipGammaToLinearConstBuffer);
	COM_RELEASE(d3d.device);
	COM_RELEASE(d3d.context);
	COM_RELEASE(d3d.swapChain);

	for(int i = 0; i < ARRAY_LEN(d3d.frameQueries); ++i)
	{
		COM_RELEASE(d3d.frameQueries[i].disjoint);
		COM_RELEASE(d3d.frameQueries[i].frameStart);
		COM_RELEASE(d3d.frameQueries[i].frameEnd);
	}

	if(d3d.library)
		FreeLibrary(d3d.library);

	memset(&d3d, 0, sizeof(d3d));

	tr.numImages = 0;
	memset(tr.images, 0, sizeof(tr.images));
}

static void BeginQueries()
{
	FrameQueries* const queries = &d3d.frameQueries[d3d.frameQueriesWriteIndex];
	queries->valid = qfalse;
	COM_RELEASE(queries->disjoint);
	COM_RELEASE(queries->frameStart);
	COM_RELEASE(queries->frameEnd);

	D3D11_QUERY_DESC qd;
	qd.MiscFlags = 0;
	qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
	d3d.device->CreateQuery(&qd, &queries->disjoint);
	qd.Query = D3D11_QUERY_TIMESTAMP;
	d3d.device->CreateQuery(&qd, &queries->frameStart);
	d3d.device->CreateQuery(&qd, &queries->frameEnd);
	if(queries->disjoint != NULL &&
	   queries->frameStart != NULL &&
	   queries->frameEnd != NULL)
	{
		queries->valid = qtrue;
		d3d.context->Begin(queries->disjoint);
		d3d.context->End(queries->frameStart);
	}
	else
	{
		COM_RELEASE(queries->disjoint);
		COM_RELEASE(queries->frameStart);
		COM_RELEASE(queries->frameEnd);
	}
}

static void EndQueries()
{
	// finish this frame
	FrameQueries* queries = &d3d.frameQueries[d3d.frameQueriesWriteIndex];
	if(queries->valid)
	{
		d3d.context->End(queries->frameEnd);
		d3d.context->End(queries->disjoint);
		d3d.frameQueriesWriteIndex = (d3d.frameQueriesWriteIndex + 1) % ARRAY_LEN(d3d.frameQueries);
	}

	// try to grab a previous frame's results
	D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = { 0 };
	backEnd.pc3D[RB_USEC_GPU] = 0; // pessimism...
	queries = &d3d.frameQueries[d3d.frameQueriesReadIndex];
	if(queries->valid &&
	   d3d.context->GetData(queries->disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
	{
		UINT64 start = 0;
		UINT64 end = 0;
		if(!disjoint.Disjoint &&
		   disjoint.Frequency > 0 &&
		   d3d.context->GetData(queries->frameStart, &start, sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
		   d3d.context->GetData(queries->frameEnd, &end, sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
		{
			backEnd.pc3D[RB_USEC_GPU] = int(((end - start) * UINT64(1000000)) / disjoint.Frequency);
		}
		d3d.frameQueriesReadIndex = (d3d.frameQueriesReadIndex + 1) % ARRAY_LEN(d3d.frameQueries);
	}
}

static void GAL_BeginFrame()
{
	BeginQueries();

	d3d.frameSeed[0] = (float)rand() / (float)RAND_MAX;
	d3d.frameSeed[1] = (float)rand() / (float)RAND_MAX;

	const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	const FLOAT clearColorDebug[4] = { 1.0f, 0.0f, 0.5f, 1.0f };
	d3d.context->ClearRenderTargetView(d3d.renderTargetViewMS, r_clear->integer ? clearColorDebug : clearColor);
	d3d.context->ClearDepthStencilView(d3d.depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	ApplyPipeline(PID_GENERIC);
	ApplyViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight, glConfig.vidHeight);
}

static void GAL_EndFrame()
{
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	if(r_fullscreen->integer == 1 && r_mode->integer == VIDEOMODE_UPSCALE)
	{
		if(r_blitMode->integer == BLITMODE_CENTERED)
		{
			scaleX = (float)glConfig.vidWidth / (float)glInfo.winWidth;
			scaleY = (float)glConfig.vidHeight / (float)glInfo.winHeight;
		}
		else if(r_blitMode->integer == BLITMODE_ASPECT)
		{
			const float ars = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
			const float ard = (float)glInfo.winWidth / (float)glInfo.winHeight;
			if(ard > ars)
			{
				scaleX = ars / ard;
				scaleY = 1.0f;
			}
			else
			{
				scaleX = 1.0f;
				scaleY = ard / ars;
			}
		}

		if(scaleX != 1.0f || scaleY != 1.0f)
		{
			const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			d3d.context->ClearRenderTargetView(d3d.backBufferRTView, clearColor);
		}
	}

	d3d.context->ResolveSubresource(d3d.resolveTexture, 0, d3d.renderTargetTextureMS, 0, d3d.formatColorRT);
	d3d.postPSData.gamma = 1.0f / r_gamma->value;
	d3d.postPSData.brightness = r_brightness->value;
	d3d.postPSData.greyscale = r_greyscale->value;
	d3d.postVSData.scaleX = scaleX;
	d3d.postVSData.scaleY = scaleY;
	ApplyPipeline(PID_POST_PROCESS);
	ApplyState(GLS_DEPTHTEST_DISABLE, CT_TWO_SIDED, qfalse);
	UploadPendingShaderData();
	BindImage(0, tr.whiteImage);
	d3d.context->PSSetShaderResources(0, 1, &d3d.resolveTextureShaderView);
	ApplySamplerState(0, TW_CLAMP_TO_EDGE, qtrue);
	ApplyViewportAndScissor(0, 0, glInfo.winWidth, glInfo.winHeight, glInfo.winHeight);
	d3d.context->Draw(3, 0);

	EndQueries();

	// Present interval flags
	// flags: DXGI_PRESENT_DO_NOT_SEQUENCE DXGI_PRESENT_ALLOW_TEARING
	const HRESULT hr = d3d.swapChain->Present(abs(r_swapInterval->integer), 0);
	if(hr == DXGI_ERROR_DEVICE_REMOVED)
	{
		ri.Error(ERR_FATAL, "Direct3D device was removed! Reason: %s", GetDeviceRemovedReason());
	}
	else if(hr == DXGI_ERROR_DEVICE_RESET)
	{
		ri.Printf(PRINT_ERROR, "Direct3D device was reset! Restarting the video system...");
		Cmd_ExecuteString("vid_restart;");
	}
}

static void GAL_BeginSkyAndClouds()
{
	const float clipPlane[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(d3d.oldSkyClipPlane, d3d.clipPlane, sizeof(d3d.oldSkyClipPlane));
	memcpy(d3d.clipPlane, clipPlane, sizeof(d3d.clipPlane));
	ApplyState(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO, CT_TWO_SIDED, qfalse);
	d3d.texEnv = TE_DISABLED;
	UploadPendingShaderData();

	UINT numVP = 1;
	d3d.context->RSGetViewports(&numVP, &d3d.oldSkyViewport);
	d3d.oldSkyViewport.MinDepth = 1.0f;
	d3d.oldSkyViewport.MaxDepth = 1.0f;
	d3d.context->RSSetViewports(1, &d3d.oldSkyViewport);
}

static void GAL_EndSkyAndClouds()
{
	d3d.oldSkyViewport.MinDepth = 0.0f;
	d3d.oldSkyViewport.MaxDepth = 1.0f;
	d3d.context->RSSetViewports(1, &d3d.oldSkyViewport);

	memcpy(d3d.clipPlane, d3d.oldSkyClipPlane, sizeof(d3d.clipPlane));
}

static void WriteInvalidImage(int w, int h, int alignment, colorSpace_t colorSpace, void* out)
{
	if(colorSpace == CS_RGBA)
		memset(out, 0x7F, PAD(w * 4, alignment) * h);
	else if(colorSpace == CS_BGR)
		memset(out, 0x7F, PAD(w * 3, alignment) * h);
}

static void GAL_ReadPixels(int x, int y, int w, int h, int alignment, colorSpace_t colorSpace, void* out)
{
	if(d3d.readbackTexture == NULL)
	{
		WriteInvalidImage(w, h, alignment, colorSpace, out);
		return;
	}

	d3d.context->CopyResource(d3d.readbackTexture, d3d.backBufferTexture);

	D3D11_MAPPED_SUBRESOURCE ms;
	HRESULT hr = d3d.context->Map(d3d.readbackTexture, 0, D3D11_MAP_READ, NULL, &ms);
	if(FAILED(hr))
	{
		WriteInvalidImage(w, h, alignment, colorSpace, out);
		return;
	}

	if(colorSpace == CS_RGBA)
	{
		const byte* srcRow = (const byte*)ms.pData;
		byte* dstRow = (byte*)out + PAD(w * 4, alignment) * (h - 1);
		for(int y = 0; y < h; ++y)
		{
			const byte* s = srcRow;
			byte* d = dstRow;
			for(int x = 0; x < w; ++x)
			{
				d[0] = s[0];
				d[1] = s[1];
				d[2] = s[2];
				d[3] = 255;
				d += 4;
				s += 4;
			}

			srcRow += ms.RowPitch;
			dstRow -= PAD(w * 4, alignment);
		}
	}
	else if(colorSpace == CS_BGR)
	{
		const byte* srcRow = (const byte*)ms.pData;
		byte* dstRow = (byte*)out + PAD(w * 3, alignment) * (h - 1);
		for(int y = 0; y < h; ++y)
		{
			const byte* s = srcRow;
			byte* d = dstRow;
			for(int x = 0; x < w; ++x)
			{
				d[2] = s[0];
				d[1] = s[1];
				d[0] = s[2];
				d += 3;
				s += 4;
			}

			srcRow += ms.RowPitch;
			dstRow -= PAD(w * 3, alignment);
		}
	}

	d3d.context->Unmap(d3d.readbackTexture, NULL);
}

static void GAL_CreateTexture(image_t* image, int mipCount, int w, int h)
{
	if(d3d.textureCount >= ARRAY_LEN(d3d.textures))
		ri.Error(ERR_FATAL, "Too many textures allocated for the Direct3D 11 back-end");

	CreateTexture(&d3d.textures[d3d.textureCount], image, mipCount, w, h);
	image->texnum = d3d.textureCount++;
}

static void GAL_UpdateTexture(image_t* image, int mip, int x, int y, int w, int h, const void* data)
{
	ID3D11Texture2D* texture = d3d.textures[image->texnum].texture;
	if(texture == NULL)
	{
		return;
	}

	const int rowBytes = image->format == TF_RGBA8 ? (w * 4) : w;
	const int imageBytes = rowBytes * h;
	D3D11_BOX box;
	box.front = 0;
	box.back = 1;
	box.left = x;
	box.right = x + w;
	box.top = y;
	box.bottom = y + h;
	d3d.context->UpdateSubresource(texture, mip, &box, data, rowBytes, imageBytes);
}

static void GAL_UpdateScratch(image_t* image, int w, int h, const void* data, qbool dirty)
{
	if(image->texnum <= 0 || image->texnum > ARRAY_LEN(d3d.textures))
	{
		return;
	}

	if(w != image->width || h != image->height)
	{
		image->width = w;
		image->height = h;
		CreateTexture(&d3d.textures[image->texnum], image, 1, w, h);
		GAL_UpdateTexture(image, 0, 0, 0, w, h, data);
	}
	else if(dirty)
	{
		GAL_UpdateTexture(image, 0, 0, 0, w, h, data);
	}
}

static void GAL_CreateTextureEx(image_t* image, int mipCount, int mipOffset, int w, int h, const void* mip0)
{
	enum { GroupSize = 8, GroupMask = GroupSize - 1 };

	// needed so we don't bind a resource that's already bound
	ID3D11ShaderResourceView* const srvNull = NULL;
	ID3D11UnorderedAccessView* const uavNull = NULL;
	ID3D11Buffer* const bufferNull = NULL;

	GAL_CreateTexture(image, mipCount - mipOffset, image->width, image->height);
	const Texture* const texture = &d3d.textures[image->texnum];

	// upload source mip 0
	const int rowBytes = w * 4;
	const int imageBytes = rowBytes * h;
	D3D11_BOX box;
	box.front = 0;
	box.back = 1;
	box.left = 0;
	box.right = w;
	box.top = 0;
	box.bottom = h;
	d3d.context->UpdateSubresource(d3d.mipGenTextures[2].texture, 0, &box, mip0, rowBytes, imageBytes);

	GammaToLinearCSData dataG2L;
	dataG2L.gamma = r_mipGenGamma->value;

	// create a linear color space copy of source mip 0
	int readIndex = 2;
	int writeIndex = 0;
	ResetShaderData(d3d.mipGammaToLinearConstBuffer, &dataG2L, sizeof(dataG2L));
	d3d.context->CSSetShader(d3d.mipGammaToLinearComputeShader, NULL, 0);
	d3d.context->CSSetConstantBuffers(0, 1, &bufferNull);
	d3d.context->CSSetShaderResources(0, 1, &srvNull);
	d3d.context->CSSetUnorderedAccessViews(0, 1, &uavNull, NULL);
	d3d.context->CSSetConstantBuffers(0, 1, &d3d.mipGammaToLinearConstBuffer);
	d3d.context->CSSetShaderResources(0, 1, &d3d.mipGenTextures[readIndex].srv);
	d3d.context->CSSetUnorderedAccessViews(0, 1, &d3d.mipGenTextures[writeIndex].uav, NULL);
	d3d.context->Dispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);

	LinearToGammaCSData dataL2G;
	dataL2G.intensity = r_intensity->value;
	dataL2G.invGamma = 1.0f / r_mipGenGamma->value;
	dataL2G.blendColor[3] = 0.0f;

	// copy to destination mip 0 now if needed
	if(mipOffset == 0)
	{
		readIndex = 0;
		writeIndex = 2;
		ResetShaderData(d3d.mipLinearToGammaConstBuffer, &dataL2G, sizeof(dataL2G));
		d3d.context->CSSetShader(d3d.mipLinearToGammaComputeShader, NULL, 0);
		d3d.context->CSSetConstantBuffers(0, 1, &bufferNull);
		d3d.context->CSSetShaderResources(0, 1, &srvNull);
		d3d.context->CSSetUnorderedAccessViews(0, 1, &uavNull, NULL);
		d3d.context->CSSetConstantBuffers(0, 1, &d3d.mipLinearToGammaConstBuffer);
		d3d.context->CSSetShaderResources(0, 1, &d3d.mipGenTextures[readIndex].srv);
		d3d.context->CSSetUnorderedAccessViews(0, 1, &d3d.mipGenTextures[writeIndex].uav, NULL);
		d3d.context->Dispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);

		box.front = 0;
		box.back = 1;
		box.left = 0;
		box.right = w;
		box.top = 0;
		box.bottom = h;
		d3d.context->CopySubresourceRegion(texture->texture, 0, 0, 0, 0, d3d.mipGenTextures[2].texture, 0, &box);
	}

	Down4CSData dataDown;
	memcpy(dataDown.weights, tr.mipFilter, sizeof(dataDown.weights));
	dataDown.clampMode = image->wrapClampMode == TW_REPEAT ? 0 : 1;

	for(int i = 1; i < mipCount; ++i)
	{
		const int w1 = w;
		const int h1 = h;
		w = max(w / 2, 1);
		h = max(h / 2, 1);
		
		// down-sample on the X-axis
		readIndex = 0;
		writeIndex = 1;
		dataDown.scale[0] = w1 / w;
		dataDown.scale[1] = 1;
		dataDown.maxSize[0] = w1 - 1;
		dataDown.maxSize[1] = h1 - 1;
		dataDown.offset[0] = 1;
		dataDown.offset[1] = 0;
		ResetShaderData(d3d.mipDownSampleConstBuffer, &dataDown, sizeof(dataDown));
		d3d.context->CSSetShader(d3d.mipDownSampleComputeShader, NULL, 0);
		d3d.context->CSSetConstantBuffers(0, 1, &bufferNull);
		d3d.context->CSSetShaderResources(0, 1, &srvNull);
		d3d.context->CSSetUnorderedAccessViews(0, 1, &uavNull, NULL);
		d3d.context->CSSetConstantBuffers(0, 1, &d3d.mipDownSampleConstBuffer);
		d3d.context->CSSetShaderResources(0, 1, &d3d.mipGenTextures[readIndex].srv);
		d3d.context->CSSetUnorderedAccessViews(0, 1, &d3d.mipGenTextures[writeIndex].uav, NULL);
		d3d.context->Dispatch((w + GroupMask) / GroupSize, (h1 + GroupMask) / GroupSize, 1);

		// down-sample on the Y-axis
		readIndex = 1;
		writeIndex = 0;
		dataDown.scale[0] = 1;
		dataDown.scale[1] = h1 / h;
		dataDown.maxSize[0] = w - 1;
		dataDown.maxSize[1] = h1 - 1;
		dataDown.offset[0] = 0;
		dataDown.offset[1] = 1;
		ResetShaderData(d3d.mipDownSampleConstBuffer, &dataDown, sizeof(dataDown));
		d3d.context->CSSetShaderResources(0, 1, &srvNull);
		d3d.context->CSSetUnorderedAccessViews(0, 1, &uavNull, NULL);
		d3d.context->CSSetShaderResources(0, 1, &d3d.mipGenTextures[readIndex].srv);
		d3d.context->CSSetUnorderedAccessViews(0, 1, &d3d.mipGenTextures[writeIndex].uav, NULL);
		d3d.context->Dispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);

		const int destMip = i - mipOffset;
		if(destMip >= 0)
		{
			// convert to final format
			readIndex = 0;
			writeIndex = 2;
			memcpy(dataL2G.blendColor, r_mipBlendColors[r_colorMipLevels->integer ? destMip : 0], sizeof(dataL2G.blendColor));
			ResetShaderData(d3d.mipLinearToGammaConstBuffer, &dataL2G, sizeof(dataL2G));
			d3d.context->CSSetShader(d3d.mipLinearToGammaComputeShader, NULL, 0);
			d3d.context->CSSetConstantBuffers(0, 1, &bufferNull);
			d3d.context->CSSetShaderResources(0, 1, &srvNull);
			d3d.context->CSSetUnorderedAccessViews(0, 1, &uavNull, NULL);
			d3d.context->CSSetConstantBuffers(0, 1, &d3d.mipLinearToGammaConstBuffer);
			d3d.context->CSSetShaderResources(0, 1, &d3d.mipGenTextures[readIndex].srv);
			d3d.context->CSSetUnorderedAccessViews(0, 1, &d3d.mipGenTextures[writeIndex].uav, NULL);
			d3d.context->Dispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);

			// write out the result
			box.front = 0;
			box.back = 1;
			box.left = 0;
			box.right = w;
			box.top = 0;
			box.bottom = h;
			d3d.context->CopySubresourceRegion(texture->texture, destMip, 0, 0, 0, d3d.mipGenTextures[2].texture, 0, &box);
		}
	}
}

static void DrawGeneric()
{
	AppendVertexData(&d3d.indexBuffer, tess.indexes, tess.numIndexes);
	if(d3d.splitBufferOffsets)
	{
		AppendVertexData(&d3d.vertexBuffers[VB_POSITION], tess.xyz, tess.numVertexes);
	}

	for(int i = 0; i < tess.shader->numStages; ++i)
	{
		const shaderStage_t* stage = tess.xstages[i];

		if(d3d.splitBufferOffsets)
		{
			AppendVertexData(&d3d.vertexBuffers[VB_TEXCOORD], tess.svars[i].texcoordsptr, tess.numVertexes);
			AppendVertexData(&d3d.vertexBuffers[VB_COLOR], tess.svars[i].colors, tess.numVertexes);
			if(stage->mtStages == 1)
			{
				AppendVertexData(&d3d.vertexBuffers[VB_TEXCOORD2], tess.svars[i + 1].texcoordsptr, tess.numVertexes);
			}
		}
		else
		{
			const void* pointers[VB_COUNT];
			pointers[VB_POSITION] = tess.xyz;
			pointers[VB_NORMAL] = NULL;
			pointers[VB_TEXCOORD] = tess.svars[i].texcoordsptr;
			pointers[VB_TEXCOORD2] = stage->mtStages == 1 ? tess.svars[i + 1].texcoordsptr : NULL;
			pointers[VB_COLOR] = tess.svars[i].colors;
			AppendVertexDataGroup(pointers, tess.numVertexes);
		}

		ApplyState(stage->stateBits, tess.shader->cullType, tess.shader->polygonOffset);

		BindBundle(0, &stage->bundle);

		if(stage->mtStages == 1)
		{
			const shaderStage_t* stage2 = tess.xstages[i + 1];
			d3d.texEnv = stage2->mtEnv;
			BindBundle(1, &stage2->bundle);
			i += 1;
		}
		else
		{
			BindImage(1, tr.whiteImage);
			d3d.texEnv = TE_DISABLED;
		}

		UploadPendingShaderData();

		DrawIndexed(tess.numIndexes);
	}

	if(tess.drawFog)
	{
		if(d3d.splitBufferOffsets)
		{
			AppendVertexData(&d3d.vertexBuffers[VB_TEXCOORD], tess.svarsFog.texcoordsptr, tess.numVertexes);
			AppendVertexData(&d3d.vertexBuffers[VB_COLOR], tess.svarsFog.colors, tess.numVertexes);
		}
		else
		{
			const void* pointers[VB_COUNT];
			pointers[VB_POSITION] = tess.xyz;
			pointers[VB_NORMAL] = NULL;
			pointers[VB_TEXCOORD] = tess.svarsFog.texcoordsptr;
			pointers[VB_TEXCOORD2] = NULL;
			pointers[VB_COLOR] = tess.svarsFog.colors;
			AppendVertexDataGroup(pointers, tess.numVertexes);
		}

		ApplyState(tess.fogStateBits, tess.shader->cullType, tess.shader->polygonOffset);

		BindImage(0, tr.fogImage);
		BindImage(1, tr.whiteImage);

		d3d.texEnv = TE_DISABLED;
		UploadPendingShaderData();

		DrawIndexed(tess.numIndexes);
	}
}

static void DrawDynamicLight()
{
	const int stageIndex = tess.shader->lightingStages[ST_DIFFUSE];
	const shaderStage_t* stage = tess.xstages[stageIndex];

	AppendVertexData(&d3d.indexBuffer, tess.dlIndexes, tess.dlNumIndexes);
	if(d3d.splitBufferOffsets)
	{
		AppendVertexData(&d3d.vertexBuffers[VB_POSITION], tess.xyz, tess.numVertexes);
		AppendVertexData(&d3d.vertexBuffers[VB_NORMAL], tess.normal, tess.numVertexes);
		AppendVertexData(&d3d.vertexBuffers[VB_TEXCOORD], tess.svars[stageIndex].texcoordsptr, tess.numVertexes);
		AppendVertexData(&d3d.vertexBuffers[VB_COLOR], tess.svars[stageIndex].colors, tess.numVertexes);
	}
	else
	{
		const void* pointers[VB_COUNT];
		pointers[VB_POSITION] = tess.xyz;
		pointers[VB_NORMAL] = tess.normal;
		pointers[VB_TEXCOORD] = tess.svars[stageIndex].texcoordsptr;
		pointers[VB_TEXCOORD2] = NULL;
		pointers[VB_COLOR] = tess.svars[stageIndex].colors;
		AppendVertexDataGroup(pointers, tess.numVertexes);
	}

	const int oldAlphaTestBits = stage->stateBits & GLS_ATEST_BITS;
	const int newBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL;
	ApplyState(oldAlphaTestBits | newBits, tess.shader->cullType, tess.shader->polygonOffset);
	BindBundle(0, &stage->bundle);

	UploadPendingShaderData();

	DrawIndexed(tess.dlNumIndexes);
}

static void DrawSoftSprite()
{
	AppendVertexData(&d3d.indexBuffer, tess.indexes, tess.numIndexes);
	if(d3d.splitBufferOffsets)
	{
		AppendVertexData(&d3d.vertexBuffers[VB_POSITION], tess.xyz, tess.numVertexes);
	}

	for(int i = 0; i < tess.shader->numStages; ++i)
	{
		const shaderStage_t* stage = tess.xstages[i];

		if(d3d.splitBufferOffsets)
		{
			AppendVertexData(&d3d.vertexBuffers[VB_TEXCOORD], tess.svars[i].texcoordsptr, tess.numVertexes);
			AppendVertexData(&d3d.vertexBuffers[VB_COLOR], tess.svars[i].colors, tess.numVertexes);
		}
		else
		{
			const void* pointers[VB_COUNT];
			pointers[VB_POSITION] = tess.xyz;
			pointers[VB_NORMAL] = NULL;
			pointers[VB_TEXCOORD] = tess.svars[i].texcoordsptr;
			pointers[VB_TEXCOORD2] = NULL;
			pointers[VB_COLOR] = tess.svars[i].colors;
			AppendVertexDataGroup(pointers, tess.numVertexes);
		}

		ApplyState(stage->stateBits, tess.shader->cullType, tess.shader->polygonOffset);

		BindBundle(0, &stage->bundle);

		UploadPendingShaderData();

		DrawIndexed(tess.numIndexes);
	}
}

static void GAL_Draw(drawType_t type)
{
	if(type == DT_GENERIC)
	{
		ApplyPipeline(PID_GENERIC);
		DrawGeneric();
	}
	else if(type == DT_DYNAMIC_LIGHT)
	{
		ApplyPipeline(PID_DYNAMIC_LIGHT);
		DrawDynamicLight();
	}
	else if(type == DT_SOFT_SPRITE)
	{
		ApplyPipeline(PID_SOFT_SPRITE);
		DrawSoftSprite();
	}
}

static void GAL_Begin2D()
{
	R_MakeIdentityMatrix(d3d.modelViewMatrix);
	R_MakeOrthoProjectionMatrix(d3d.projectionMatrix, glConfig.vidWidth, glConfig.vidHeight);
	ApplyViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight, glConfig.vidHeight);
	ApplyState(GLS_DEFAULT_2D, CT_TWO_SIDED, qfalse);
}

static void GAL_Begin3D()
{
	ApplyPipeline(PID_GENERIC);
	memcpy(d3d.projectionMatrix, backEnd.viewParms.projectionMatrix, sizeof(d3d.projectionMatrix));
	ApplyViewportAndScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY, backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight, glConfig.vidHeight);

	if(backEnd.refdef.rdflags & RDF_HYPERSPACE)
	{
		const FLOAT c = 0.25 + 0.5 * sin(M_PI * (backEnd.refdef.time & 0x01FF) / 0x0200);
		const FLOAT clearColor[4] = { c, c, c, 1.0f };
		d3d.context->ClearRenderTargetView(d3d.backBufferRTView, clearColor);
		return;
	}

	d3d.context->ClearDepthStencilView(d3d.depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	if(r_fastsky->integer && !(backEnd.refdef.rdflags & RDF_NOWORLDMODEL))
	{
		const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		d3d.context->ClearRenderTargetView(d3d.backBufferRTView, clearColor);
	}

	if(backEnd.viewParms.isPortal)
	{
		float plane[4];
		plane[0] = backEnd.viewParms.portalPlane.normal[0];
		plane[1] = backEnd.viewParms.portalPlane.normal[1];
		plane[2] = backEnd.viewParms.portalPlane.normal[2];
		plane[3] = backEnd.viewParms.portalPlane.dist;

		float plane2[4];
		plane2[0] = DotProduct(backEnd.viewParms.orient.axis[0], plane);
		plane2[1] = DotProduct(backEnd.viewParms.orient.axis[1], plane);
		plane2[2] = DotProduct(backEnd.viewParms.orient.axis[2], plane);
		plane2[3] = DotProduct(plane, backEnd.viewParms.orient.origin) - plane[3];

		float* o = plane;
		const float* m = s_flipMatrix;
		const float* v = plane2;
		o[0] = m[0] * v[0] + m[4] * v[1] + m[ 8] * v[2] + m[12] * v[3];
		o[1] = m[1] * v[0] + m[5] * v[1] + m[ 9] * v[2] + m[13] * v[3];
		o[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3];
		o[3] = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];

		memcpy(d3d.clipPlane, plane, sizeof(d3d.clipPlane));
	}
	else
	{
		const float clipPlane[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		memcpy(d3d.clipPlane, clipPlane, sizeof(d3d.clipPlane));
	}

	ApplyState(GLS_DEFAULT, CT_TWO_SIDED, qfalse);
}

static void GAL_SetModelViewMatrix(const float* matrix)
{
	memcpy(d3d.modelViewMatrix, matrix, sizeof(d3d.modelViewMatrix));
}

static void GAL_SetDepthRange(double near, double far)
{
	D3D11_VIEWPORT viewport;
	UINT numVP = 1;
	d3d.context->RSGetViewports(&numVP, &viewport);

	viewport.MinDepth = (float)near;
	viewport.MaxDepth = (float)far;
	d3d.context->RSSetViewports(1, &viewport);
}

static void GAL_BeginDynamicLight()
{
	const dlight_t* const dl = tess.light;

	d3d.osEyePos[0] = backEnd.orient.viewOrigin[0];
	d3d.osEyePos[1] = backEnd.orient.viewOrigin[1];
	d3d.osEyePos[2] = backEnd.orient.viewOrigin[2];
	d3d.osEyePos[3] = 1.0f;
	d3d.osLightPos[0] = dl->transformed[0];
	d3d.osLightPos[1] = dl->transformed[1];
	d3d.osLightPos[2] = dl->transformed[2];
	d3d.osLightPos[3] = 1.0f;
	d3d.lightColor[0] = dl->color[0];
	d3d.lightColor[1] = dl->color[1];
	d3d.lightColor[2] = dl->color[2];
	d3d.lightRadius = 1.0f / Square(dl->radius);
}

static void GAL_PrintInfo()
{
	ri.Printf(PRINT_ALL, "Direct3D device feature level: %s\n", d3d.device->GetFeatureLevel() == D3D_FEATURE_LEVEL_11_0 ? "11.0" : "10.1");
	ri.Printf(PRINT_ALL, "Direct3D vertex buffer upload strategy: %s\n", d3d.splitBufferOffsets ? "split offsets" : "sync'd offsets");
	if(d3d.adapterInfo.valid)
	{
		ri.Printf(PRINT_ALL, "%6d MB of dedicated GPU memory\n", d3d.adapterInfo.dedicatedVideoMemoryMB);
		ri.Printf(PRINT_ALL, "%6d MB of shared system memory\n", d3d.adapterInfo.sharedSystemMemoryMB);
		ri.Printf(PRINT_ALL, "%6d MB of dedicated system memory\n", d3d.adapterInfo.dedicatedSystemMemoryMB);
	}
}

qbool GAL_GetD3D11(graphicsAPILayer_t* rb)
{
	rb->Init = &GAL_Init;
	rb->ShutDown = &GAL_ShutDown;
	rb->BeginSkyAndClouds = &GAL_BeginSkyAndClouds;
	rb->EndSkyAndClouds = &GAL_EndSkyAndClouds;
	rb->ReadPixels = &GAL_ReadPixels;
	rb->BeginFrame = &GAL_BeginFrame;
	rb->EndFrame = &GAL_EndFrame;
	rb->CreateTexture = &GAL_CreateTexture;
	rb->UpdateTexture = &GAL_UpdateTexture;
	rb->UpdateScratch = &GAL_UpdateScratch;
	rb->CreateTextureEx = &GAL_CreateTextureEx;
	rb->Draw = &GAL_Draw;
	rb->Begin2D = &GAL_Begin2D;
	rb->Begin3D = &GAL_Begin3D;
	rb->SetModelViewMatrix = &GAL_SetModelViewMatrix;
	rb->SetDepthRange = &GAL_SetDepthRange;
	rb->BeginDynamicLight = &GAL_BeginDynamicLight;
	rb->PrintInfo = &GAL_PrintInfo;

	return qtrue;
}


#else


#include "tr_local.h"


qbool GAL_GetD3D11(graphicsAPILayer_t* rb)
{
	return qfalse;
}


#endif
