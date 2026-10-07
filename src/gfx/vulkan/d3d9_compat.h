#pragma once

#ifdef __SWITCH__

#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include <cstring>
#include <algorithm>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_vi.h>
#include <mojoshader.h>

#ifndef __cdecl
#define __cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __declspec
#define __declspec(x)
#endif

using HRESULT = int32_t;
using _D3DFORMAT = uint32_t;
using _D3DMULTISAMPLE_TYPE = uint32_t;

constexpr HRESULT S_OK = 0;
constexpr HRESULT S_FALSE = 1;
constexpr HRESULT E_FAIL = -1;

struct HINSTANCE__ {};
struct HWND__ {};
using HWND = HWND__*;

struct tagRECT { int32_t left=0, top=0, right=0, bottom=0; };
using RECT = tagRECT;

struct IDirect3DSwapChain9 { void Release() { delete this; } };

constexpr _D3DFORMAT D3DFMT_UNKNOWN = 0;
constexpr _D3DFORMAT D3DFMT_A8 = 1;
constexpr _D3DFORMAT D3DFMT_R5G6B5 = 23;
constexpr _D3DFORMAT D3DFMT_A1R5G5B5 = 25;
constexpr _D3DFORMAT D3DFMT_A8B8G8R8 = 32;
constexpr _D3DFORMAT D3DFMT_L8 = 50;
constexpr _D3DFORMAT D3DFMT_A8L8 = 51;
constexpr _D3DFORMAT D3DFMT_D16_LOCKABLE = 70;
constexpr _D3DFORMAT D3DFMT_D16 = 80;
constexpr _D3DFORMAT D3DFMT_D24FS8 = 83;
constexpr _D3DFORMAT D3DFMT_D24S8 = 75;
constexpr _D3DFORMAT D3DFMT_D24X8 = 77;
constexpr _D3DFORMAT D3DFMT_D15S1 = 73;
constexpr _D3DFORMAT D3DFMT_A8R8G8B8 = 21;
constexpr _D3DFORMAT D3DFMT_X8R8G8B8 = 22;
constexpr _D3DFORMAT D3DFMT_G16R16F = 112;
constexpr _D3DFORMAT D3DFMT_R32F = 114;
constexpr _D3DFORMAT D3DFMT_DXT1 = 0x31545844u;
constexpr _D3DFORMAT D3DFMT_DXT3 = 0x33545844u;
constexpr _D3DFORMAT D3DFMT_DXT5 = 0x35545844u;
constexpr uint32_t D3DFMT_INDEX16 = 101;
constexpr uint32_t D3DLOCK_NOOVERWRITE = 0x1000;
constexpr uint32_t D3DLOCK_DISCARD = 0x2000;
constexpr uint32_t D3DISSUE_END = 0x1;
constexpr uint32_t D3DISSUE_BEGIN = 0x2;
constexpr uint32_t D3DMULTISAMPLE_NONE = 0;
constexpr uint32_t D3DDEVTYPE_HAL = 1;
constexpr uint32_t D3DRTYPE_SURFACE = 8;
constexpr uint32_t D3DBACKBUFFER_TYPE_MONO = 1;
constexpr uint32_t D3DPOOL_DEFAULT = 0;
constexpr uint32_t D3DZB_FALSE = 0;
constexpr uint32_t D3DZB_TRUE = 1;
constexpr uint32_t D3DCLEAR_TARGET = 0x1;
constexpr uint32_t D3DCLEAR_ZBUFFER = 0x2;
constexpr uint32_t D3DCLEAR_STENCIL = 0x4;
constexpr uint32_t D3DPT_TRIANGLELIST = 4;
constexpr uint32_t D3DFILL_SOLID = 3;
constexpr uint32_t D3DFILL_WIREFRAME = 2;

enum _D3DTEXTUREFILTERTYPE : uint32_t {
    D3DTEXF_NONE = 0,
    D3DTEXF_POINT = 1,
    D3DTEXF_LINEAR = 2,
    D3DTEXF_ANISOTROPIC = 3
};

struct _D3DLOCKED_BOX { void *pBits=nullptr; int RowPitch=0; int SlicePitch=0; };
struct _D3DBOX { uint32_t Left=0, Top=0, Front=0, Right=0, Bottom=0, Back=0; };
struct _D3DDISPLAYMODE { uint32_t Width=0, Height=0, RefreshRate=60; _D3DFORMAT Format=D3DFMT_X8R8G8B8; };

enum _D3DCUBEMAP_FACES : uint32_t {
    D3DCUBEMAP_FACE_POSITIVE_X=0, D3DCUBEMAP_FACE_NEGATIVE_X=1,
    D3DCUBEMAP_FACE_POSITIVE_Y=2, D3DCUBEMAP_FACE_NEGATIVE_Y=3,
    D3DCUBEMAP_FACE_POSITIVE_Z=4, D3DCUBEMAP_FACE_NEGATIVE_Z=5
};
using D3DCUBEMAP_FACES = _D3DCUBEMAP_FACES;

struct D3DVIEWPORT9 { uint32_t X,Y,Width,Height; float MinZ,MaxZ; };
using _D3DVIEWPORT9 = D3DVIEWPORT9;

constexpr uint32_t D3DRS_ZENABLE = 7;
constexpr uint32_t D3DRS_FILLMODE = 8;
constexpr uint32_t D3DRS_ZWRITEENABLE = 14;
constexpr uint32_t D3DRS_ALPHATESTENABLE = 15;
constexpr uint32_t D3DRS_SRCBLEND = 19;
constexpr uint32_t D3DRS_DESTBLEND = 20;
constexpr uint32_t D3DRS_CULLMODE = 22;
constexpr uint32_t D3DRS_ZFUNC = 23;
constexpr uint32_t D3DRS_ALPHAREF = 24;
constexpr uint32_t D3DRS_ALPHAFUNC = 25;
constexpr uint32_t D3DRS_ALPHABLENDENABLE = 27;
constexpr uint32_t D3DRS_ADAPTIVETESS_Y = 62;
constexpr uint32_t D3DRS_STENCILENABLE = 52;
constexpr uint32_t D3DRS_STENCILFAIL = 53;
constexpr uint32_t D3DRS_STENCILZFAIL = 54;
constexpr uint32_t D3DRS_STENCILPASS = 55;
constexpr uint32_t D3DRS_STENCILFUNC = 56;
constexpr uint32_t D3DRS_STENCILREF = 57;
constexpr uint32_t D3DRS_STENCILMASK = 58;
constexpr uint32_t D3DRS_STENCILWRITEMASK = 59;
constexpr uint32_t D3DRS_COLORWRITEENABLE = 168;
constexpr uint32_t D3DRS_BLENDOP = 171;
constexpr uint32_t D3DRS_SCISSORTESTENABLE = 174;
constexpr uint32_t D3DRS_SLOPESCALEDEPTHBIAS = 175;
constexpr uint32_t D3DRS_TWOSIDEDSTENCILMODE = 185;
constexpr uint32_t D3DRS_CCW_STENCILFAIL = 186;
constexpr uint32_t D3DRS_CCW_STENCILZFAIL = 187;
constexpr uint32_t D3DRS_CCW_STENCILPASS = 188;
constexpr uint32_t D3DRS_CCW_STENCILFUNC = 189;
constexpr uint32_t D3DRS_DEPTHBIAS = 195;
constexpr uint32_t D3DRS_SEPARATEALPHABLENDENABLE = 206;
constexpr uint32_t D3DRS_SRCBLENDALPHA = 207;
constexpr uint32_t D3DRS_DESTBLENDALPHA = 208;
constexpr uint32_t D3DRS_BLENDOPALPHA = 209;

enum : uint32_t {
    D3DSAMP_ADDRESSU=1, D3DSAMP_ADDRESSV=2, D3DSAMP_ADDRESSW=3,
    D3DSAMP_MAGFILTER=5, D3DSAMP_MINFILTER=6, D3DSAMP_MIPFILTER=7,
    D3DSAMP_MIPMAPLODBIAS=8, D3DSAMP_MAXANISOTROPY=10
};

enum : uint8_t {
    D3DDECLTYPE_FLOAT1=1, D3DDECLTYPE_FLOAT2=2, D3DDECLTYPE_FLOAT3=3,
    D3DDECLTYPE_FLOAT4=4, D3DDECLTYPE_D3DCOLOR=5, D3DDECLTYPE_UBYTE4=6,
    D3DDECLTYPE_SHORT2=7, D3DDECLTYPE_SHORT4=8, D3DDECLTYPE_UBYTE4N=9,
    D3DDECLTYPE_SHORT2N=10, D3DDECLTYPE_SHORT4N=11, D3DDECLTYPE_USHORT2N=12,
    D3DDECLTYPE_USHORT4N=13, D3DDECLTYPE_UDEC3=14, D3DDECLTYPE_DEC3N=15,
    D3DDECLTYPE_FLOAT16_2=16, D3DDECLTYPE_FLOAT16_4=17
};

struct _D3DVERTEXELEMENT9 {
    uint16_t Stream; uint16_t Offset; uint8_t Type; uint8_t Method;
    uint8_t Usage; uint8_t UsageIndex;
};

struct _D3DLOCKED_RECT { void *pBits=nullptr; int Pitch=0; };

struct VulkanUniformLayout
{
    uint32_t floatCount = 0;
    uint32_t intCount = 0;
    uint32_t boolCount = 0;
    size_t floatOffset = 0;
    size_t intOffset = 0;
    size_t boolOffset = 0;
    size_t size = 0;
};

struct KisakVkBuffer {
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    void *mapped=nullptr;
    std::vector<uint8_t> shadow;
    uint32_t size=0;
    VkDevice device=VK_NULL_HANDLE;
    void Release();
    HRESULT Lock(uint32_t offset, uint32_t sizeBytes, void **out, uint32_t flags);
    HRESULT Unlock();
};

struct KisakVkTexture {
    VkImage image=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width=0, height=0, depth=1, mipLevels=1, arrayLayers=1;
    _D3DFORMAT sourceFormat=D3DFMT_UNKNOWN;
    std::vector<VkImageLayout> subresourceLayouts;
    uint32_t refs=1;
    std::vector<uint8_t> lockShadow;
    bool lockShadowActive=false;
    void AddRef() { ++refs; }
    VkImageLayout GetSubresourceLayout(uint32_t level, uint32_t layer=0) const
    {
        const size_t index = static_cast<size_t>(layer) * mipLevels + level;
        return index < subresourceLayouts.size()
            ? subresourceLayouts[index] : layout;
    }
    void SetSubresourceLayout(uint32_t level, uint32_t layer, VkImageLayout newLayout)
    {
        const size_t index = static_cast<size_t>(layer) * mipLevels + level;
        if (index >= subresourceLayouts.size())
            subresourceLayouts.resize(static_cast<size_t>(arrayLayers) * mipLevels,
                                      VK_IMAGE_LAYOUT_UNDEFINED);
        subresourceLayouts[index] = newLayout;
        layout = newLayout;
    }
    HRESULT LockRect(uint32_t level, _D3DLOCKED_RECT *lockedRect, const tagRECT*, uint32_t);
    HRESULT UnlockRect(uint32_t level);
    HRESULT LockBox(uint32_t level, _D3DLOCKED_BOX *lockedBox, const _D3DBOX*, uint32_t);
    HRESULT UnlockBox(uint32_t level);
    HRESULT AddDirtyBox(const _D3DBOX*) { return S_OK; }
    void Release();
};

using IDirect3DVertexBuffer9 = KisakVkBuffer;
using IDirect3DIndexBuffer9 = KisakVkBuffer;

struct KisakVkVertexDeclaration {
    std::vector<_D3DVERTEXELEMENT9> elements;
    // Material_BuildVertexDecl records the engine vertex declaration type so
    // Vulkan can distinguish the generic GfxVertex 2D layout from packed
    // world-vertex layouts that intentionally share the D3D source table.
    uint8_t switchVertDeclType = 0xFF;
};
using IDirect3DVertexDeclaration9 = KisakVkVertexDeclaration;

struct KisakVkShader {
    VkShaderModule module=VK_NULL_HANDLE;
    VkShaderStageFlagBits stage=VK_SHADER_STAGE_VERTEX_BIT;
    const MOJOSHADER_parseData *parseData=nullptr;
    std::vector<uint32_t> spirv;
    const void *linkedDeclaration=nullptr;
    const void *linkedPixelShader=nullptr;
    uint32_t alphaFuncSpecId=0;
    uint32_t alphaRefSpecId=0;
    void Release();
};
using IDirect3DVertexShader9 = KisakVkShader;
using IDirect3DPixelShader9 = KisakVkShader;

using IDirect3DBaseTexture9 = KisakVkTexture;
using IDirect3DTexture9 = KisakVkTexture;
using IDirect3DVolumeTexture9 = KisakVkTexture;
using IDirect3DCubeTexture9 = KisakVkTexture;
using LPDIRECT3DTEXTURE9 = IDirect3DTexture9*;

struct IDirect3DSurface9 {
    KisakVkTexture *texture=nullptr;
    uint32_t level=0;
    bool defaultFramebuffer=false;
    uint32_t refs=1;
    void AddRef();
    HRESULT Release();
    HRESULT LockRect(_D3DLOCKED_RECT*, const tagRECT*, uint32_t);
    HRESULT UnlockRect();
};

struct IDirect3DQuery9 {
    bool begun=false, issued=false;
    HRESULT Issue(uint32_t flags) {
        if (flags==D3DISSUE_BEGIN) { if (begun) return E_FAIL; begun=true; return S_OK; }
        if (flags==D3DISSUE_END) { if (!begun) return E_FAIL; begun=false; issued=true; return S_OK; }
        return E_FAIL;
    }
    HRESULT GetData(void *data, uint32_t size, uint32_t) {
        if (!issued) return S_FALSE;
        uint64_t value=1;
        if (data && size) std::memcpy(data, &value, std::min<uint32_t>(size, sizeof(value)));
        return S_OK;
    }
    void Release() { delete this; }
};

struct IDirect3D9 {};

class VulkanBackend;
class IDirect3DDevice9;
using LPDIRECT3DDEVICE9 = IDirect3DDevice9 *;

class IDirect3DDevice9 {
public:
    IDirect3DDevice9();
    ~IDirect3DDevice9();

    HRESULT StretchRect(IDirect3DSurface9*, const tagRECT*, IDirect3DSurface9*, const tagRECT*, _D3DTEXTUREFILTERTYPE);
    HRESULT CreateOffscreenPlainSurface(uint32_t,uint32_t,_D3DFORMAT,uint32_t,IDirect3DSurface9**,void*);
    HRESULT BeginScene();
    HRESULT EndScene();
    HRESULT CreateDepthStencilSurface(uint32_t,uint32_t,_D3DFORMAT,_D3DMULTISAMPLE_TYPE,uint32_t,uint32_t,IDirect3DSurface9**,void*);
    HRESULT CreateRenderTarget(uint32_t,uint32_t,_D3DFORMAT,_D3DMULTISAMPLE_TYPE,uint32_t,uint32_t,IDirect3DSurface9**,void*);
    HRESULT SetRenderTarget(uint32_t,IDirect3DSurface9*);
    HRESULT SetDepthStencilSurface(IDirect3DSurface9*);
    HRESULT CreateVertexBuffer(uint32_t,uint32_t,uint32_t,uint32_t,IDirect3DVertexBuffer9**,void*);
    HRESULT CreateIndexBuffer(uint32_t,uint32_t,_D3DFORMAT,uint32_t,IDirect3DIndexBuffer9**,void*);
    HRESULT CreateVertexDeclaration(const _D3DVERTEXELEMENT9*,IDirect3DVertexDeclaration9**);
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9*);
    HRESULT SetIndices(IDirect3DIndexBuffer9*);
    HRESULT SetStreamSource(uint32_t,IDirect3DVertexBuffer9*,uint32_t,uint32_t);
    HRESULT SetTexture(uint32_t,IDirect3DBaseTexture9*);
    HRESULT CreateVertexShader(const void*,uint32_t,IDirect3DVertexShader9**);
    HRESULT CreatePixelShader(const void*,uint32_t,IDirect3DPixelShader9**);
    HRESULT CreateVertexShader(const void*,IDirect3DVertexShader9**);
    HRESULT CreatePixelShader(const void*,IDirect3DPixelShader9**);
    HRESULT SetVertexShader(IDirect3DVertexShader9*);
    HRESULT SetPixelShader(IDirect3DPixelShader9*);
    HRESULT SetVertexShaderConstantF(uint32_t,const float*,uint32_t);
    HRESULT SetPixelShaderConstantF(uint32_t,const float*,uint32_t);
    HRESULT SetVertexShaderConstantI(uint32_t,const int32_t*,uint32_t);
    HRESULT SetPixelShaderConstantI(uint32_t,const int32_t*,uint32_t);
    HRESULT SetVertexShaderConstantB(uint32_t,const int32_t*,uint32_t);
    HRESULT SetPixelShaderConstantB(uint32_t,const int32_t*,uint32_t);
    HRESULT SetViewport(const D3DVIEWPORT9*);
    HRESULT SetSwitchUnlitMode(bool);
    HRESULT SetSamplerState(uint32_t,uint32_t,uint32_t);
    HRESULT SetScissorRect(const tagRECT*);
    HRESULT SetRenderState(uint32_t,uint32_t);
    HRESULT DrawPrimitiveUP(uint32_t,uint32_t,const void*,uint32_t);
    HRESULT DrawIndexedPrimitive(uint32_t,int32_t,uint32_t,uint32_t,uint32_t,uint32_t);
    HRESULT UpdateTexture(IDirect3DVolumeTexture9*,IDirect3DVolumeTexture9*);
    HRESULT TestCooperativeLevel();
    HRESULT Clear(uint32_t,uint32_t,uint32_t,uint32_t,float,uint32_t);

private:
    struct StreamBinding { IDirect3DVertexBuffer9 *buffer=nullptr; uint32_t offset=0, stride=0; };
    VulkanBackend *m_backend=nullptr;
    IDirect3DSurface9 *m_color=nullptr, *m_depth=nullptr;
    IDirect3DVertexDeclaration9 *m_decl=nullptr;
    StreamBinding m_streams[16]{};
    IDirect3DIndexBuffer9 *m_indices=nullptr;
    IDirect3DVertexShader9 *m_vertexShader=nullptr;
    IDirect3DPixelShader9 *m_pixelShader=nullptr;
    IDirect3DBaseTexture9 *m_textures[16]{};
    std::array<std::array<float,4>,256> m_vsFloat{}, m_psFloat{};
    std::array<std::array<int32_t,4>,256> m_vsInt{}, m_psInt{};
    std::array<int32_t,256> m_vsBool{}, m_psBool{};
    uint32_t m_samplerState[16][16]{};
    D3DVIEWPORT9 m_viewport{0,0,1280,720,0.0f,1.0f};
    bool m_switchUnlit=false;
    bool m_depthEnable=true, m_depthWrite=true, m_blendEnable=false;
    bool m_separateAlphaBlend=false;
    VkPrimitiveTopology m_topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    uint32_t m_srcBlend=2, m_dstBlend=1, m_srcBlendAlpha=2, m_dstBlendAlpha=1;
    uint32_t m_blendOp=1, m_blendOpAlpha=1, m_cullMode=2, m_depthFunc=3;
    bool m_alphaTest=false;
    uint32_t m_alphaFunc=8, m_alphaRef=0;
    bool m_scissor=false;
    tagRECT m_scissorRect{0,0,1280,720};
    uint32_t m_colorWriteMask=0xF;
    float m_depthBias=0.0f, m_slopeDepthBias=0.0f;
    bool m_stencilEnable=false;
    uint32_t m_stencilFunc=8, m_stencilRef=0, m_stencilMask=0xffffffffu, m_stencilWriteMask=0xffffffffu;
    uint32_t m_stencilFail=1, m_stencilZFail=1, m_stencilPass=1;
    bool m_twoSidedStencil=false;
    uint32_t m_ccwStencilFunc=8, m_ccwStencilFail=1, m_ccwStencilZFail=1, m_ccwStencilPass=1;
    bool m_sceneOpen=false;
    bool m_targetRendered=false;
    bool m_pipelineDirty=true;
    std::unordered_map<uint64_t, VkPipeline> m_pipelines;
    bool BindDescriptorSets();
    bool PrepareDraw();
    bool EnsurePipeline();
    bool BindUniformSet(
        VkPipelineBindPoint bindPoint, uint32_t setIndex,
        const MOJOSHADER_parseData *parse,
        const void *floatData, const void *intData, const void *boolData);
    bool BindSamplerSet(
        VkPipelineBindPoint bindPoint, uint32_t setIndex,
        IDirect3DBaseTexture9 *const (&textures)[16]);
};

#endif
