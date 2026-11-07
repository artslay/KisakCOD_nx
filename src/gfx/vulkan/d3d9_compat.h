#pragma once

#ifdef __SWITCH__

#include <atomic>
#include <atomic>
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
constexpr _D3DFORMAT D3DFMT_X1R5G5B5 = 24;
constexpr _D3DFORMAT D3DFMT_A1R5G5B5 = 25;
constexpr _D3DFORMAT D3DFMT_A2B10G10R10 = 31;
constexpr _D3DFORMAT D3DFMT_A8B8G8R8 = 32;
constexpr _D3DFORMAT D3DFMT_X8B8G8R8 = 33;
constexpr _D3DFORMAT D3DFMT_G16R16 = 34;
constexpr _D3DFORMAT D3DFMT_A2R10G10B10 = 35;
constexpr _D3DFORMAT D3DFMT_A16B16G16R16 = 36;
constexpr _D3DFORMAT D3DFMT_L8 = 50;
constexpr _D3DFORMAT D3DFMT_A8L8 = 51;
constexpr _D3DFORMAT D3DFMT_D16_LOCKABLE = 70;
constexpr _D3DFORMAT D3DFMT_D16 = 80;
constexpr _D3DFORMAT D3DFMT_D32 = 71;
constexpr _D3DFORMAT D3DFMT_L16 = 81;
constexpr _D3DFORMAT D3DFMT_D24FS8 = 83;
constexpr _D3DFORMAT D3DFMT_D24S8 = 75;
constexpr _D3DFORMAT D3DFMT_D24X8 = 77;
constexpr _D3DFORMAT D3DFMT_D15S1 = 73;
constexpr _D3DFORMAT D3DFMT_A8R8G8B8 = 21;
constexpr _D3DFORMAT D3DFMT_X8R8G8B8 = 22;
constexpr _D3DFORMAT D3DFMT_R16F = 111;
constexpr _D3DFORMAT D3DFMT_G16R16F = 112;
constexpr _D3DFORMAT D3DFMT_A16B16G16R16F = 113;
constexpr _D3DFORMAT D3DFMT_R32F = 114;
constexpr _D3DFORMAT D3DFMT_G32R32F = 115;
constexpr _D3DFORMAT D3DFMT_A32B32G32R32F = 116;
constexpr _D3DFORMAT D3DFMT_DXT1 = 0x31545844u;
constexpr _D3DFORMAT D3DFMT_DXT3 = 0x33545844u;
constexpr _D3DFORMAT D3DFMT_DXT5 = 0x35545844u;
// IW3 DXN images use the ATI2/3Dc two-channel block encoding (BC5).
constexpr _D3DFORMAT D3DFMT_DXN = 0x32495441u;
// Legacy CoD4/NVIDIA transparency-AA render-format FOURCC values passed
// through D3DRS_ADAPTIVETESS_Y by the original D3D9 renderer.
constexpr _D3DFORMAT D3DFMT_SSAA = 0x41415353u;
constexpr _D3DFORMAT D3DFMT_ATOC = 0x434F5441u;
constexpr uint32_t D3DFMT_INDEX16 = 101;
constexpr uint32_t D3DLOCK_READONLY = 0x10;
constexpr uint32_t D3DLOCK_NOSYSLOCK = 0x800;
constexpr uint32_t D3DLOCK_NOOVERWRITE = 0x1000;
constexpr uint32_t D3DLOCK_DISCARD = 0x2000;
constexpr uint32_t D3DLOCK_DONOTWAIT = 0x4000;
constexpr uint32_t D3DLOCK_NO_DIRTY_UPDATE = 0x8000;
constexpr uint32_t D3DISSUE_END = 0x1;
constexpr uint32_t D3DISSUE_BEGIN = 0x2;
constexpr uint32_t D3DMULTISAMPLE_NONE = 0;
constexpr uint32_t D3DMULTISAMPLE_NONMASKABLE = 1;
constexpr uint32_t D3DMULTISAMPLE_2_SAMPLES = 2;
constexpr uint32_t D3DMULTISAMPLE_4_SAMPLES = 4;
constexpr uint32_t D3DMULTISAMPLE_8_SAMPLES = 8;
constexpr uint32_t D3DMULTISAMPLE_16_SAMPLES = 16;
constexpr uint32_t D3DDEVTYPE_HAL = 1;
constexpr uint32_t D3DRTYPE_SURFACE = 8;
constexpr uint32_t D3DBACKBUFFER_TYPE_MONO = 1;
constexpr uint32_t D3DPOOL_DEFAULT = 0;
constexpr uint32_t D3DPOOL_MANAGED = 1;
constexpr uint32_t D3DPOOL_SYSTEMMEM = 2;
constexpr uint32_t D3DPOOL_SCRATCH = 3;
constexpr uint32_t D3DZB_FALSE = 0;
constexpr uint32_t D3DZB_TRUE = 1;
constexpr uint32_t D3DCLEAR_TARGET = 0x1;
constexpr uint32_t D3DCLEAR_ZBUFFER = 0x2;
constexpr uint32_t D3DCLEAR_STENCIL = 0x4;
constexpr uint32_t D3DPT_TRIANGLELIST = 4;
constexpr uint32_t D3DFILL_POINT = 1;
constexpr uint32_t D3DFILL_WIREFRAME = 2;
constexpr uint32_t D3DFILL_SOLID = 3;

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
constexpr uint32_t D3DRS_MULTISAMPLEANTIALIAS = 161;
constexpr uint32_t D3DRS_MULTISAMPLEMASK = 162;
constexpr uint32_t D3DRS_BLENDFACTOR = 193;
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
    // MojoShader's Vulkan profile densely packs used constant registers into
    // the UBO arrays. Preserve the original D3D register for each packed slot.
    uint32_t floatCount = 0;
    uint32_t intCount = 0;
    uint32_t boolCount = 0;
    std::array<uint16_t, 256> floatRegisters{};
    std::array<uint16_t, 256> intRegisters{};
    std::array<uint16_t, 256> boolRegisters{};
    size_t floatOffset = 0;
    size_t intOffset = 0;
    size_t boolOffset = 0;
    size_t size = 0;
};

struct IDirect3DResource9
{
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
    virtual ~IDirect3DResource9() = default;
};

struct KisakVkBuffer : IDirect3DResource9 {
    KisakVkBuffer();
    ~KisakVkBuffer() override;
    std::atomic<uint32_t> refs{1};
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    void *mapped=nullptr;
    uint32_t AddRef() override
    {
        return refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    // Dynamic buffers keep a staging copy; static buffers are written directly
    // to their persistently mapped HOST_VISIBLE|HOST_COHERENT allocation.
    std::vector<uint8_t> shadow;
    std::vector<uint8_t> recoveryShadow;
    VkBufferUsageFlags usageFlags=0;
    VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    bool recoveryPending=false;
    bool dynamic=false;
    // Dynamic shadow storage remains authoritative while in-frame uploads are queued.
    bool shadowInitialized=false;
    uint32_t size=0;
    bool lockActive=false;
    uint32_t lockOffset=0, lockSize=0, lockFlags=0;
    bool meshWriteTrackingPending=false, lockMeshWriteTracking=false;
    // Exact byte span reserved through R_ReserveMeshVerts during this lock.
    uint32_t lockWriteBegin=UINT32_MAX, lockWriteEnd=0;
    VkDevice device=VK_NULL_HANDLE;
    uint32_t Release() override;
    HRESULT Lock(uint32_t offset, uint32_t sizeBytes, void **out, uint32_t flags);
    HRESULT Unlock();
    void BeginMeshWriteTracking();
    void MarkWrittenRange(uint32_t offset, uint32_t bytes);
};

struct GfxImage;

struct KisakVkTexture : IDirect3DResource9 {
    // Copied from the owning GfxImage so Switch logs identify texture assets
    // even when their first upload never happened.
    char debugName[96] = "<unlabeled>";
    GfxImage *sourceImage = nullptr;
    VkImage image=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkImageView baseMipView=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkImage resolveImage=VK_NULL_HANDLE;
    VkImageView resolveView=VK_NULL_HANDLE;
    VkDeviceMemory resolveMemory=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
    VkImageViewType viewType=VK_IMAGE_VIEW_TYPE_2D;
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout resolveLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    uint32_t width=0, height=0, depth=1, mipLevels=1, arrayLayers=1;
    _D3DFORMAT sourceFormat=D3DFMT_UNKNOWN;
    std::vector<VkImageLayout> subresourceLayouts;
    // Most sampled images keep the same subresource layouts across many draws.
    // Cache the mip-chain summary and invalidate it only when a layout changes.
    uint64_t subresourceLayoutRevision = 1;
    uint64_t subresourceLayoutSummaryRevision = 0;
    VkImageLayout subresourceLayoutSummaryFallback = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t subresourceLayoutSummaryMipLevels = 0;
    uint32_t subresourceLayoutSummaryArrayLayers = 0;
    bool subresourceLayoutSummaryBaseInitialized = false;
    bool subresourceLayoutSummaryAllInitialized = false;
    bool subresourceLayoutSummaryBaseShaderRead = false;
    bool subresourceLayoutSummaryAllShaderRead = false;
    // D3D9 volume-texture dirty regions are tracked in base-level texels.
    bool dirtyAll = true;
    std::vector<_D3DBOX> dirtyBoxes;
    std::atomic<uint32_t> refs{1};
    std::vector<uint8_t> lockShadow;
    // Keep the last CPU copy of a locked volume mip between LockBox calls.
    // Model-lighting patches update small regions every frame; re-reading the
    // full volume from Vulkan on every lock needlessly stalls the renderer.
    std::atomic<bool> volumeShadowValid{false};
    std::atomic<uint32_t> volumeShadowLevel{0xffffffffu};
    bool lockShadowActive=false;
    bool lockIsVolume=false;
    bool lockWasUndefined=false;
    uint32_t lockLevel=0xffffffffu;
    uint32_t lockFlags=0;
    uint32_t lockLeft=0, lockTop=0, lockFront=0;
    uint32_t lockRight=0, lockBottom=0, lockBack=0;
    VkImageLayout lockOriginalLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t AddRef() override
    {
        return refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    VkImageLayout GetSubresourceLayout(uint32_t level, uint32_t layer=0) const
    {
        if (level >= mipLevels || layer >= arrayLayers)
            return VK_IMAGE_LAYOUT_UNDEFINED;
        const size_t index = static_cast<size_t>(layer) * mipLevels + level;
        return index < subresourceLayouts.size()
            ? subresourceLayouts[index] : layout;
    }
    void SetSubresourceLayout(uint32_t level, uint32_t layer, VkImageLayout newLayout)
    {
        if (level >= mipLevels || layer >= arrayLayers)
            return;
        const size_t index = static_cast<size_t>(layer) * mipLevels + level;
        if (index >= subresourceLayouts.size())
        {
            subresourceLayouts.resize(static_cast<size_t>(arrayLayers) * mipLevels,
                                      VK_IMAGE_LAYOUT_UNDEFINED);
            ++subresourceLayoutRevision;
        }
        if (subresourceLayouts[index] != newLayout)
        {
            subresourceLayouts[index] = newLayout;
            ++subresourceLayoutRevision;
        }
        layout = newLayout;
    }
    void GetSubresourceLayoutSummary(
        bool *baseMipInitialized,
        bool *allSubresourcesInitialized,
        bool *baseMipShaderRead,
        bool *allSubresourcesShaderRead)
    {
        const size_t subresourceCount =
            static_cast<size_t>(arrayLayers) * mipLevels;
        if (subresourceLayouts.size() < subresourceCount)
        {
            // Preserve the previous sampler path's fallback for entries that
            // have not yet received explicit per-subresource layout tracking.
            subresourceLayouts.resize(subresourceCount, layout);
            ++subresourceLayoutRevision;
        }

        if (subresourceLayoutSummaryRevision != subresourceLayoutRevision ||
            subresourceLayoutSummaryFallback != layout ||
            subresourceLayoutSummaryMipLevels != mipLevels ||
            subresourceLayoutSummaryArrayLayers != arrayLayers)
        {
            bool baseInitialized = true;
            bool allInitialized = true;
            bool baseShaderRead = true;
            bool allShaderRead = true;
            for (uint32_t layer = 0; layer < arrayLayers; ++layer)
            {
                for (uint32_t level = 0; level < mipLevels; ++level)
                {
                    const VkImageLayout subresourceLayout =
                        GetSubresourceLayout(level, layer);
                    if (level == 0)
                    {
                        if (subresourceLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                            baseInitialized = false;
                        if (subresourceLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                            baseShaderRead = false;
                    }
                    if (subresourceLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                        allInitialized = false;
                    if (subresourceLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                        allShaderRead = false;
                }
            }

            subresourceLayoutSummaryBaseInitialized = baseInitialized;
            subresourceLayoutSummaryAllInitialized = allInitialized;
            subresourceLayoutSummaryBaseShaderRead = baseShaderRead;
            subresourceLayoutSummaryAllShaderRead = allShaderRead;
            subresourceLayoutSummaryFallback = layout;
            subresourceLayoutSummaryMipLevels = mipLevels;
            subresourceLayoutSummaryArrayLayers = arrayLayers;
            subresourceLayoutSummaryRevision = subresourceLayoutRevision;
        }

        if (baseMipInitialized)
            *baseMipInitialized = subresourceLayoutSummaryBaseInitialized;
        if (allSubresourcesInitialized)
            *allSubresourcesInitialized = subresourceLayoutSummaryAllInitialized;
        if (baseMipShaderRead)
            *baseMipShaderRead = subresourceLayoutSummaryBaseShaderRead;
        if (allSubresourcesShaderRead)
            *allSubresourcesShaderRead = subresourceLayoutSummaryAllShaderRead;
    }
    HRESULT LockRect(uint32_t level, _D3DLOCKED_RECT *lockedRect, const tagRECT*, uint32_t);
    HRESULT UnlockRect(uint32_t level);
    HRESULT LockBox(uint32_t level, _D3DLOCKED_BOX *lockedBox, const _D3DBOX*, uint32_t);
    HRESULT UnlockBox(uint32_t level);
    HRESULT AddDirtyRect(const tagRECT *rect);
    HRESULT AddDirtyRect(uint32_t face, const tagRECT *rect);
    HRESULT AddDirtyBox(const _D3DBOX *box);
    uint32_t Release() override;
};

using IDirect3DVertexBuffer9 = KisakVkBuffer;
using IDirect3DIndexBuffer9 = KisakVkBuffer;

struct KisakVkVertexDeclaration : IDirect3DResource9 {
    std::atomic<uint32_t> refs{1};
    uint64_t serial=0;
    std::vector<_D3DVERTEXELEMENT9> elements;
    uint32_t AddRef() override
    {
        return refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    uint32_t Release() override
    {
        const uint32_t oldRefs = refs.fetch_sub(1, std::memory_order_acq_rel);
        if (oldRefs == 1)
        {
            delete this;
            return 0;
        }
        return oldRefs - 1;
    }
    // Material_BuildVertexDecl records the engine vertex declaration type so
    // Vulkan can distinguish the generic GfxVertex 2D layout from packed
    // world-vertex layouts that intentionally share the D3D source table.
    uint8_t switchVertDeclType = 0xFF;
};
using IDirect3DVertexDeclaration9 = KisakVkVertexDeclaration;

struct KisakVkShader : IDirect3DResource9 {
    KisakVkShader();
    ~KisakVkShader() override;
    std::atomic<uint32_t> refs{1};
    uint64_t serial=0;
    VkShaderModule module=VK_NULL_HANDLE;
    VkShaderStageFlagBits stage=VK_SHADER_STAGE_VERTEX_BIT;
    const MOJOSHADER_parseData *parseData=nullptr;
    // MojoShader uniform register packing is immutable for this shader.
    // Build it once instead of walking parseData on every draw.
    VulkanUniformLayout uniformLayout{};
    bool uniformLayoutCached=false;
    bool uniformLayoutValid=false;
    std::vector<uint32_t> spirv;
    // The interface-linked fragment module before the optional alpha-test rewrite.
    // Alpha testing is dynamic D3D9 state, so retain this baseline to rebuild the
    // fragment module when the state changes without re-linking both shader stages.
    std::vector<uint32_t> linkedSpirv;
    uint64_t linkedDeclarationSerial=0;
    uint64_t linkedPixelShaderSerial=0;
    uint64_t linkedVertexShaderSerial=0;
    // The shared VERTDECL_PACKED route has a different POSITION0 format for
    // FX code meshes because their authored binormalSign lives in position.w.
    bool linkedFxCodeMesh=false;
    // Cache which pixel-shader sampler slots implement CoD4 HSM depth taps.
    uint32_t sunShadowCompareShaderMask=UINT32_MAX;
    bool sunShadowCompareBindingTraceLogged=false;
    bool sunShadowCompareSamplerTraceLogged=false;
    // The fragment SPIR-V module variant linked for the currently bound sun map.
    uint32_t linkedSunShadowCompareMask=UINT32_MAX;
    uint32_t AddRef() override
    {
        return refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    bool linkedAlphaTestPatch=false;
    // The injected alpha comparison opcode and reference are baked into the
    // fragment SPIR-V; rebuild when either D3D9 state value changes.
    uint32_t linkedAlphaFunc=UINT32_MAX;
    uint32_t linkedAlphaRef=UINT32_MAX;
    uint32_t alphaFuncSpecId=0;
    uint32_t alphaRefSpecId=0;
    uint32_t Release() override;
};
using IDirect3DVertexShader9 = KisakVkShader;
using IDirect3DPixelShader9 = KisakVkShader;

using IDirect3DBaseTexture9 = KisakVkTexture;
using IDirect3DTexture9 = KisakVkTexture;
using IDirect3DVolumeTexture9 = KisakVkTexture;
using IDirect3DCubeTexture9 = KisakVkTexture;
using LPDIRECT3DTEXTURE9 = IDirect3DTexture9*;

class VulkanBackend;

struct IDirect3DSurface9 : IDirect3DResource9 {
    KisakVkTexture *texture=nullptr;
    uint32_t level=0;
    bool defaultFramebuffer=false;
    bool lockable=false;
    std::atomic<uint32_t> refs{1};
    uint32_t AddRef() override;
    uint32_t Release() override;
    HRESULT LockRect(_D3DLOCKED_RECT*, const tagRECT*, uint32_t);
    HRESULT UnlockRect();
};

constexpr uint32_t D3DQUERYTYPE_EVENT = 8;
constexpr uint32_t D3DQUERYTYPE_OCCLUSION = 9;

struct IDirect3DQuery9 : IDirect3DResource9 {
    std::atomic<uint32_t> refs{1};
    uint32_t AddRef() override
    {
        return refs.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    VulkanBackend *backend = nullptr;
    void *query = nullptr;
    uint32_t queryType = 0;
    bool begun = false;
    bool issued = false;

    static IDirect3DQuery9 *Create(VulkanBackend *backend, uint32_t queryType);
    HRESULT Issue(uint32_t flags);
    HRESULT GetData(void *data, uint32_t size, uint32_t flags);
    uint32_t Release() override;
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
    void SetMarkMeshPassActive(bool active)
    {
        if (m_markMeshPassActive != active)
        {
            m_markMeshPassActive = active;
            m_pipelineDirty = true;
        }
    }
    void SetParticleCloudPassActive(bool active)
    {
        if (m_particleCloudPassActive != active)
        {
            m_particleCloudPassActive = active;
            m_pipelineDirty = true;
        }
    }
    HRESULT SetSamplerState(uint32_t,uint32_t,uint32_t);
    HRESULT SetScissorRect(const tagRECT*);
    HRESULT SetRenderState(uint32_t,uint32_t);
    HRESULT DrawPrimitiveUP(uint32_t,uint32_t,const void*,uint32_t);
    HRESULT DrawIndexedPrimitive(uint32_t,int32_t,uint32_t,uint32_t,uint32_t,uint32_t);
    HRESULT UpdateTexture(IDirect3DVolumeTexture9*,IDirect3DVolumeTexture9*);
    HRESULT TestCooperativeLevel();
    HRESULT Clear(uint32_t,const tagRECT*,uint32_t,uint32_t,float,uint32_t);

private:
    struct StreamBinding { IDirect3DVertexBuffer9 *buffer=nullptr; uint32_t offset=0, stride=0; };
    struct SamplerDescriptorCacheEntry
    {
        VkDescriptorSet set = VK_NULL_HANDLE;
        std::array<VkDescriptorImageInfo, 16> images{};
    };
    struct UniformBindCacheEntry
    {
        uint64_t frameSerial = 0;
        uint64_t revision = 0;
        uint64_t shaderSerial = 0;
        VkDevice device = VK_NULL_HANDLE;
        const MOJOSHADER_parseData *parse = nullptr;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    VulkanBackend *m_backend=nullptr;
    IDirect3DSurface9 *m_color=nullptr, *m_depth=nullptr;
    IDirect3DVertexDeclaration9 *m_decl=nullptr;
    StreamBinding m_streams[16]{};
    // DrawPrimitiveUP supplies a transient stream-0 buffer and its own stride;
    // EnsurePipeline must describe that input rather than a stale SetStreamSource.
    bool m_drawPrimitiveUPActive=false;
    uint32_t m_drawPrimitiveUPStride=0;
    IDirect3DIndexBuffer9 *m_indices=nullptr;
    IDirect3DVertexShader9 *m_vertexShader=nullptr;
    IDirect3DPixelShader9 *m_pixelShader=nullptr;
    IDirect3DBaseTexture9 *m_textures[16]{};
    // Exact per-stage image views written to the most recently built sampler descriptors.
    VkImageView m_lastSamplerDescriptorViews[2][16]{};
    uint8_t m_lastSamplerDescriptorStates[2][16]{};
    uint8_t m_lastSamplerDescriptorActive[2][16]{};
    uint64_t m_lastSamplerDescriptorShaderSerials[2]{};
    uint64_t m_lastSamplerDescriptorFrameSerials[2]{};
    std::array<std::array<float,4>,256> m_vsFloat{}, m_psFloat{};
    std::array<std::array<int32_t,4>,256> m_vsInt{}, m_psInt{};
    std::array<int32_t,256> m_vsBool{}, m_psBool{};
    uint32_t m_samplerState[16][16]{};
    D3DVIEWPORT9 m_viewport{0,0,1280,720,0.0f,1.0f};
    bool m_switchUnlit=false;
    bool m_markMeshPassActive=false;
    // Procedural particle-cloud quads are camera-facing FX geometry and must
    // remain visible from either side, independent of the material cull state.
    bool m_particleCloudPassActive=false;
    bool m_alphaToCoverage=false;
    bool m_depthEnable=true, m_depthWrite=true, m_blendEnable=false;
    uint32_t m_multisampleMask=0xffffffffu;
    uint32_t m_blendFactor=0xffffffffu;
    bool m_separateAlphaBlend=false;
    VkPrimitiveTopology m_topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    uint32_t m_fillMode=D3DFILL_SOLID;
    uint32_t m_srcBlend=2, m_dstBlend=1, m_srcBlendAlpha=2, m_dstBlendAlpha=1;
    uint32_t m_blendOp=1, m_blendOpAlpha=1, m_cullMode=3, m_depthFunc=4;
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
    // Keep descriptor sets alive across frames: NVK's pool reset walks and
    // frees every set, which currently exposes allocator-list corruption.
    // Descriptor sets are isolated per in-flight frame slot. The GPU may
    // still consume the previous slot while the CPU rewrites this slot's sets.
    std::array<std::array<std::vector<VkDescriptorSet>,2>,2> m_samplerDescriptorSets{};
    std::array<std::array<std::vector<VkDescriptorSet>,2>,2> m_uniformDescriptorSets{};
    // Immutable sampler descriptor snapshots, deduplicated within a frame.
    // Descriptor sets are never rewritten after a recorded draw has referenced them.
    std::array<std::array<
        std::unordered_map<uint64_t, std::vector<SamplerDescriptorCacheEntry>>,2>,2>
        m_samplerDescriptorFrameCache{};
    std::array<std::array<size_t,2>,2> m_samplerSetCursor{};
    std::array<std::array<size_t,2>,2> m_uniformSetCursor{};
    std::array<uint64_t,2> m_descriptorCacheFrameSerial{};
    std::array<std::array<UniformBindCacheEntry,2>,2> m_uniformBindCache{};
    uint64_t m_vsUniformRevision = 1;
    uint64_t m_psUniformRevision = 1;
    // Vulkan command-buffer state persists across draws and dynamic-rendering
    // restarts. Avoid re-emitting identical pipeline, descriptor and dynamic state.
    uint64_t m_cachedCommandFrameSerial = ~uint64_t(0);
    uint64_t m_cachedCommandRecordingSerial = ~uint64_t(0);
    VkDevice m_cachedCommandDevice = VK_NULL_HANDLE;
    VkCommandBuffer m_cachedCommandBuffer = VK_NULL_HANDLE;
    VkPipeline m_cachedBoundPipeline = VK_NULL_HANDLE;
    std::array<VkDescriptorSet,4> m_cachedBoundDescriptorSets{};
    VkViewport m_cachedViewport{};
    VkRect2D m_cachedScissor{};
    std::array<float,4> m_cachedBlendConstants{};
    bool m_cachedViewportValid = false;
    bool m_cachedScissorValid = false;
    bool m_cachedBlendConstantsValid = false;
    bool m_cachedPipelineFastPathValid = false;
    uint64_t m_cachedPipelineStateKey = 0;
    VkPipeline m_cachedPipelineState = VK_NULL_HANDLE;
    VkDevice m_cachedPipelineDevice = VK_NULL_HANDLE;
    const GfxImage *m_cachedPipelineSunShadowImage = nullptr;
    VkFormat m_cachedPipelineColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_cachedPipelineDepthFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits m_cachedPipelineColorSamples = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits m_cachedPipelineDepthSamples = VK_SAMPLE_COUNT_1_BIT;
    bool m_pipelineDirty=true;
    std::unordered_map<uint64_t, VkPipeline> m_pipelines;
    void LogGpuDrawCommand(const char *kind, uint32_t primitiveType,
        uint32_t primitiveCount, uint32_t vertexCount, uint32_t indexCount,
        uint32_t startIndex, int32_t baseVertexIndex);
    void ResetCachedCommandState();
    void BindDescriptorSetIfChanged(
        VkPipelineBindPoint bindPoint, uint32_t setIndex, VkDescriptorSet set);
    void BindPipelineIfChanged(VkPipeline pipeline);
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

bool Switch_PrepareVulkanDeviceLossResources(VulkanBackend *backend);
bool Switch_RestoreVulkanBuffers(VulkanBackend *backend);

#endif
