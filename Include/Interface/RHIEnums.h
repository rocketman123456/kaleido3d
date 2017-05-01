#ifndef __RHICommonDefs_h__
#define __RHICommonDefs_h__

#include "../Config/PlatformTypes.h"
#include "../KTL/DynArray.hpp"
#include "../KTL/String.hpp"

/**
* Global Enums
* Must Start with 'E'
**/

K3D_COMMON_NS
{

enum ERHIType : uint8
{
  ERHI_Vulkan,
  ERHI_Metal,
  ERHI_D3D12,
  ERHI_OpenGL,
};

enum ECommandType : uint8
{
  ECMD_Bundle,
  ECMD_Graphics,
  ECMD_Compute,
  ECommandTypeNum
};

enum ECommandUsageType : uint8
{
  ECMDUsage_OneShot,
  ECMDUsage_ReUsable
};

enum EPipelineType : uint8
{
  EPSO_Compute,
  EPSO_Graphics
};

enum EPixelFormat : uint8
{
  EPF_RGBA16Uint,
  EPF_RGBA32Float,
  EPF_RGBA8Unorm,
  EPF_RGBA8Unorm_sRGB,
  EPF_R11G11B10Float,
  EPF_D32Float,
  EPF_RGB32Float,
  EPF_RGB8Unorm,
  EPF_BGRA8Unorm, // Apple Metal Layer uses it as default pixel format
  EPF_BGRA8Unorm_sRGB,
  EPF_RGBA16Float,
  PixelFormatNum,
};

enum EVertexFormat : uint8
{
  EVF_Float1x32,
  EVF_Float2x32,
  EVF_Float3x32,
  EVF_Float4x32,
  VertexFormatNum
};

enum EVertexInputRate : uint8
{
  EVIR_PerVertex,
  EVIR_PerInstance
};

enum EMultiSampleFlag : uint8
{
  EMS_1x,
  EMS_2x,
  EMS_4x,
  EMS_8x,
  EMS_16x,
  EMS_32x,
  ENumMultiSampleFlag
};

enum EPrimitiveType : uint8
{
  EPT_Points,
  EPT_Lines,
  EPT_Triangles,
  EPT_TriangleStrip,
  PrimTypeNum
};

enum EShaderType : uint8
{
  ES_Fragment,
  ES_Vertex,
  ES_Geometry,
  ES_Hull,
  ES_Domain,
  ES_Compute,
  ShaderTypeNum
};

enum ELoadAction : uint8
{
  ELA_Load,
  ELA_Clear,
  ELA_DontCare
};

enum EStoreAction : uint8
{
  ESA_Store,
  ESA_DontCare
};

/*
VK_IMAGE_LAYOUT_GENERAL / D3D12_RESOURCE_STATE_COMMON
VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL / D3D12_RESOURCE_STATE_RENDER_TARGET
VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL /
D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_DEPTH_READ
VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL /
D3D12_RESOURCE_STATE_DEPTH_READ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL /
D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
D3D12_RESOURCE_STATE_UNORDERED_ACCESS VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL /
D3D12_RESOURCE_STATE_COPY_SOURCE VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL /
D3D12_RESOURCE_STATE_COPY_DEST VK_IMAGE_LAYOUT_PRESENT_SRC_KHR /
D3D12_RESOURCE_STATE_PRESENT
*/
enum EResourceState : uint8
{
  ERS_Common,
  ERS_Present, // D3D12_RESOURCE_STATE_PRESENT/VK_IMAGE_LAYOUT_PRESENT_SRC_KHR?
  ERS_RenderTarget, // D3D12_RESOURCE_STATE_RENDER_TARGET/VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL?
  ERS_ShaderResource,
  ERS_TransferDst,
  ERS_TransferSrc,
  ERS_RWDepthStencil,
  ERS_Unknown,
  ERS_Num
};

enum EPipelineStage : uint8
{
  EPS_Default
};

enum EGpuMemViewType : uint8
{
  EGVT_Undefined,
  EGVT_VBV, // For VertexBuffer
  EGVT_IBV, // For IndexBuffer
  EGVT_CBV, // For ConstantBuffer,
  EGVT_SRV, // For Texture
  EGVT_UAV, // For Buffer
  EGVT_RTV,
  EGVT_DSV,
  EGVT_Sampler,
  EGVT_SOV,
  GpuViewTypeNum
};

enum EGpuResourceType : uint8
{
  EGT_Texture1D,
  EGT_Texture1DArray,
  EGT_Texture2D,
  EGT_Texture2DArray,
  EGT_Texture2DMS,
  EGT_Texture2DMSArray,
  EGT_Texture3D,
  EGT_TextureCube,
  EGT_Buffer,
  ResourceTypeNum
};

enum EGpuResourceAccessFlag : uint8
{
  EGRAF_Read = 0x1,
  EGRAF_Write = 0x1 << 1,
  EGRAF_ReadAndWrite = 0x3,
  EGRAF_HostVisible = 0x1 << 2,
  EGRAF_DeviceVisible = 0x1 << 3,
  EGRAF_HostCoherent = 0x1 << 4,
  EGRAF_HostCached = 0x1 << 5,
};

inline EGpuResourceAccessFlag operator|(EGpuResourceAccessFlag const& lhs,
  EGpuResourceAccessFlag const& rhs)
{
  return EGpuResourceAccessFlag(uint32(lhs) | uint32(rhs));
}

enum EGpuResourceCreationFlag : uint8
{
  EGRCF_Dynamic = 0,
  EGRCF_Static = 1,
  EGRCF_TransferSrc = 2,
  EGRCF_TransferDst = 4
};

inline EGpuResourceCreationFlag operator|(EGpuResourceCreationFlag const& lhs,
  EGpuResourceCreationFlag const& rhs)
{
  return EGpuResourceCreationFlag(uint32(lhs) | uint32(rhs));
}

enum EResourceOrigin : uint8
{
  ERO_User,
  ERO_Swapchain,
};

/**
* Same as VkImageAspectFlagBits
*/
enum ETextureAspectFlag : uint8
{
  ETAF_COLOR = 1,
  ETAF_DEPTH = 1 << 1,
  ETAF_STENCIL = 1 << 2,
  ETAF_METADATA = 1 << 3,
};

inline ETextureAspectFlag operator|(ETextureAspectFlag const& lhs,
  ETextureAspectFlag const& rhs)
{
  return ETextureAspectFlag(uint32(lhs) | uint32(rhs));
}
}

#endif