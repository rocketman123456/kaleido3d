#ifndef __RHIStructs_h__
#define __RHIStructs_h__

#include "RHIEnums.h"
#include "ShaderCommon.h"

K3D_COMMON_NS
{
  /* for vulkan
  enum EPipelineStage
  {
  EPS_Top				= 0x1,
  EPS_VertexShader	= 0x8,
  EPS_PixelShader		= 0x80,
  EPS_PixelOutput		= 0x400,
  EPS_ComputeShader	= 0x800,
  EPS_Transfer		= 0x1000,
  EPS_Bottom			= 0x2000,
  EPS_Graphics		= 0x8000,
  EPS_All				= 0x10000
  };*/

  /**
   * @see GfxSetting
   */
  struct RenderTargetFormat
  {
    RenderTargetFormat()
    {
      NumRTs = 1;
      RenderPixelFormats = new EPixelFormat{ EPF_RGBA8Unorm };
      DepthPixelFormat = EPF_RGBA8Unorm;
      MSAACount = 1;
    }
    uint32 NumRTs;
    EPixelFormat* RenderPixelFormats;
    EPixelFormat DepthPixelFormat;
    uint32 MSAACount;
  };

  struct BlendState
  {
    enum EOperation : uint8
    {
      Add,
      Sub,
      BlendOpNum
    };
    enum EBlend : uint8
    {
      Zero,
      One,
      SrcColor,
      DestColor,
      SrcAlpha,
      DestAlpha,
      BlendTypeNum
    };

    bool Enable;
    uint32 ColorWriteMask;

    EBlend Src;
    EBlend Dest;
    EOperation Op;

    EBlend SrcBlendAlpha;
    EBlend DestBlendAlpha;
    EOperation BlendAlphaOp;

    BlendState()
    {
      Enable = false;
      Src = EBlend::One;
      Dest = EBlend::Zero;
      Op = EOperation::Add;

      SrcBlendAlpha = EBlend::One;
      DestBlendAlpha = EBlend::Zero;
      BlendAlphaOp = EOperation::Add;
      ColorWriteMask = 0xf;
    }
  };

  struct AttachmentState
  {
    BlendState Blend;
    EPixelFormat Format; // for metal
  };

  struct RasterizerState
  {
    enum EFillMode : uint8
    {
      WireFrame,
      Solid,
      FillModeNum
    };
    enum ECullMode : uint8
    {
      None,
      Front,
      Back,
      CullModeNum
    };

    EFillMode FillMode;
    ECullMode CullMode;
    bool FrontCCW;
    int32 DepthBias;
    float DepthBiasClamp;
    bool DepthClipEnable;
    bool MultiSampleEnable;
    EMultiSampleFlag MSFlag;

    RasterizerState()
    {
      FillMode = EFillMode::Solid;
      CullMode = ECullMode::Back;
      FrontCCW = false;
      DepthBias = 0;
      DepthBiasClamp = 0.0f;
      DepthClipEnable = true;
      MultiSampleEnable = false;
    }
  };

  struct DepthStencilState
  {
    enum EStencilOp : uint8
    {
      Keep,
      Zero,
      Replace,
      Invert,
      Increment,
      Decrement,
      StencilOpNum
    };

    enum EComparisonFunc : uint8
    {
      Never,
      Less,
      Equal,
      LessEqual,
      Greater,
      NotEqual,
      GreaterEqual,
      Always,
      ComparisonFuncNum
    };

    enum EDepthWriteMask : uint8
    {
      WriteZero,
      WriteAll,
      DepthWriteMaskNum
    };

    struct Op
    {
      EStencilOp StencilFailOp;
      EStencilOp DepthStencilFailOp;
      EStencilOp StencilPassOp;
      EComparisonFunc StencilFunc;

      Op()
      {
        StencilFailOp = EStencilOp::Keep;
        DepthStencilFailOp = EStencilOp::Keep;
        StencilPassOp = EStencilOp::Keep;
        StencilFunc = EComparisonFunc::Always;
      }
    };

    bool DepthEnable;
    EDepthWriteMask DepthWriteMask;
    EComparisonFunc DepthFunc;

    bool StencilEnable;
    uint8 StencilReadMask;
    uint8 StencilWriteMask;
    Op FrontFace;
    Op BackFace;

    DepthStencilState()
    {
      DepthEnable = false;
      DepthWriteMask = EDepthWriteMask::WriteAll;
      DepthFunc = EComparisonFunc::Less;

      StencilEnable = false;
      StencilReadMask = 0xff;
      StencilWriteMask = 0xff;
    }
  };

  struct SamplerState
  {
    typedef DepthStencilState::EComparisonFunc EComparisonFunc;
    enum EFilterMethod : uint8
    {
      Point, // Nearest
      Linear,
      FilterMethodNum
    };
    enum EFilterReductionType : uint8
    {
      Standard,
      Comparison, // all three other filter should be linear
      Minimum,
      Maximum,
      FilterReductionTypeNum
    };
    enum ETextureAddressMode : uint8
    {
      Wrap,
      Mirror, // Repeat
      Clamp,
      Border,
      MirrorOnce,
      AddressModeNum
    };

    struct Filter
    {
      EFilterMethod MinFilter;
      EFilterMethod MagFilter;
      EFilterMethod MipMapFilter;
      EFilterReductionType ReductionType;
      Filter()
      {
        MinFilter = EFilterMethod::Linear;
        MagFilter = EFilterMethod::Linear;
        MipMapFilter = EFilterMethod::Linear;
        ReductionType = EFilterReductionType::Standard;
      }
    };

    Filter Filter;
    ETextureAddressMode U, V, W;
    float MipLODBias;
    uint32 MaxAnistropy;
    EComparisonFunc ComparisonFunc;
    float BorderColor[4];
    float MinLOD;
    float MaxLOD;

    SamplerState()
      : U(ETextureAddressMode::Wrap)
      , V(ETextureAddressMode::Wrap)
      , W(ETextureAddressMode::Wrap)
      , MipLODBias(0)
      , MaxAnistropy(16)
      , ComparisonFunc(EComparisonFunc::LessEqual)
      , MinLOD(0)
      , MaxLOD(3.402823466e+38f)
    {
    }
  };

  struct VertexInputState
  {
    enum
    {
      kInvalidValue = -1,
      kMaxVertexLayouts = 4,
      kMaxVertexBindings = 8,
    };

    struct Attribute
    {
      Attribute(EVertexFormat const& format = EVF_Float3x32,
        uint32 offset = kInvalidValue,
        uint32 slot = kInvalidValue)
        : Format(format)
        , OffSet(offset)
        , Slot(slot)
      {
      }

      EVertexFormat Format /* = EVF_Float3x32*/;
      uint32 OffSet /* = kInvalidValue*/;
      uint32 Slot /* = kInvalidValue*/;
    };

    struct Layout
    {
      Layout(EVertexInputRate const& inputRate = EVIR_PerVertex,
        uint32 stride = kInvalidValue)
        : Rate(inputRate)
        , Stride(stride)
      {
      }

      EVertexInputRate Rate /* = EVIR_PerVertex */;
      uint32 Stride /* = kInvalidValue*/;
    };

    VertexInputState() {}

    Attribute Attribs[kMaxVertexBindings];
    Layout Layouts[kMaxVertexLayouts];
  };

  using AttachmentStateArray = k3d::DynArray<AttachmentState>;

  struct RenderPipelineStateDesc
  {
    RenderPipelineStateDesc()
      : PrimitiveTopology(EPT_Triangles)
      , PatchControlPoints(0)
      , DepthAttachmentFormat()
    {
    }
    RasterizerState Rasterizer;
    AttachmentStateArray AttachmentsBlend;  // 
    DepthStencilState DepthStencil;
    EPixelFormat DepthAttachmentFormat;     // for metal
    VertexInputState InputState;
    // InputAssemblyState
    EPrimitiveType PrimitiveTopology /* = rhi::EPT_Triangles */;
    // Tessellation Patch
    uint32 PatchControlPoints /* = 0*/;

    ShaderBundle VertexShader;
    ShaderBundle PixelShader;
    ShaderBundle GeometryShader;
    ShaderBundle DomainShader;
    ShaderBundle HullShader;
  };

  struct Rect
  {
    long Left;
    long Top;
    long Right;
    long Bottom;
  };

  struct ViewportDesc
  {
    ViewportDesc()
      : ViewportDesc(0.f, 0.f)
    {
    }

    ViewportDesc(float width,
                 float height,
                 float left = 0.0f,
                 float top = 0.0f,
                 float minDepth = 0.0f,
                 float maxDepth = 1.0f)
      : Left(left)
      , Top(top)
      , Width(width)
      , Height(height)
      , MinDepth(minDepth)
      , MaxDepth(maxDepth)
    {
    }

    float Left;
    float Top;
    float Width;
    float Height;
    float MinDepth;
    float MaxDepth;
  };

  struct VertexBufferView
  {
    uint64 BufferLocation;
    uint32 SizeInBytes;
    uint32 StrideInBytes;
  };

  struct IndexBufferView
  {
    uint64 BufferLocation;
    uint32 SizeInBytes;
  };

  struct SwapChainDesc
  {
    EPixelFormat Format;
    uint32 Width;
    uint32 Height;
    uint32 NumBuffers;
  };

  /**
   * Format, Width, Height, Depth, MipLevel, Layers
   */
  struct TextureDesc
  {
    EPixelFormat Format;
    uint32 Width;
    uint32 Height;
    uint32 Depth;
    uint32 MipLevels;
    uint32 Layers;

    TextureDesc()
    {
      Format = EPixelFormat::EPF_RGBA8Unorm;
      Width = 0;
      Height = 0;
      Depth = 0;
      MipLevels = 0;
      Layers = 0;
    }

    bool IsTex1D() const { return Depth == 1 && Layers == 1 && Height == 1; }
    bool IsTex2D() const { return Depth == 1 && Layers == 1 && Height > 1; }
    bool IsTex3D() const { return Depth > 1 && Layers == 1; }
  };

  /* in d3d12 and vulkan, each resource has a memory for resource foot print
   * which needs extra memory. */
  struct ResourceDesc
  {
    EGpuResourceType Type;
    EGpuResourceAccessFlag Flag;
    EGpuResourceCreationFlag CreationFlag;
    EGpuMemViewType ViewType;
    EResourceOrigin Origin;
    union
    {
      TextureDesc TextureDesc;
      uint64 Size;
    };
    ResourceDesc()
      : Type(EGT_Buffer)
      , Flag(EGRAF_ReadAndWrite)
      , CreationFlag(EGRCF_Dynamic)
      , ViewType(EGVT_SRV)
      , Origin(ERO_User)
    {
    }
    ResourceDesc(EGpuResourceType t,
                 EGpuResourceAccessFlag f,
                 EGpuResourceCreationFlag cf,
                 EGpuMemViewType viewType)
      : Type(t)
      , Flag(f)
      , CreationFlag(cf)
      , ViewType(viewType)
      , Origin(ERO_User)
    {
    }
  };

  /*same as VkImageSubresource */
  struct TextureResourceSpec
  {
    ETextureAspectFlag Aspect;
    uint32 MipLevel;
    uint32 ArrayLayer;
  };

  struct ResourceViewDesc
  {
    EGpuMemViewType ViewType;
    TextureResourceSpec TextureSpec;
  };
  /**
   * Same as VkSubresourceLayout
   */
  struct SubResourceLayout
  {
    uint64 Offset = 0;
    uint64 Size = 0;
    uint64 RowPitch = 0;
    uint64 ArrayPitch = 0;
    uint64 DepthPitch = 0;
  };

  struct CopyBufferRegion
  {
    uint64 SrcOffSet;
    uint64 DestOffSet;
    uint64 CopySize;
  };

  /* see also VkBufferImageCopy(vk) and
   * D3D12_PLACED_SUBRESOURCE_FOOTPRINT(d3d12)
   */
  struct SubResourceFootprint
  {
    TextureDesc Dimension;
    SubResourceLayout SubLayout;
  };

  struct PlacedSubResourceFootprint
  {
    uint32 BufferOffSet = 0; // VkBufferImageCopy#bufferOffset
    int32 TOffSetX = 0, TOffSetY = 0,
          TOffSetZ = 0;                  // VkBufferImageCopy#imageOffset
    SubResourceFootprint Footprint = {}; // VkBufferImageCopy#imageExtent
  };

  /**
   * IndexCountPerInstance
   * InstanceCount
   * StartLocation
   * BaseVertexLocation
   * StartInstanceLocation
   */
  struct DrawIndexedInstancedParam
  {
    DrawIndexedInstancedParam(uint32 indexCountPerInst,
                              uint32 instCount,
                              uint32 startLoc = 0,
                              uint32 baseVerLoc = 0,
                              uint32 sInstLoc = 1)
      : IndexCountPerInstance(indexCountPerInst)
      , InstanceCount(instCount)
      , StartIndexLocation(startLoc)
      , BaseVertexLocation(baseVerLoc)
      , StartInstanceLocation(sInstLoc)
    {
    }
    uint32 IndexCountPerInstance;
    uint32 InstanceCount;
    uint32 StartIndexLocation;
    uint32 BaseVertexLocation;
    uint32 StartInstanceLocation;
  };

  struct DrawInstancedParam
  {
    DrawInstancedParam(uint32 vertexPerInst,
                       uint32 instances,
                       uint32 startLocation = 0,
                       uint32 startInstLocation = 0)
      : VertexCountPerInstance(vertexPerInst)
      , InstanceCount(instances)
      , StartVertexLocation(startLocation)
      , StartInstanceLocation(startInstLocation)
    {
    }
    uint32 VertexCountPerInstance;
    uint32 InstanceCount;
    uint32 StartVertexLocation;
    uint32 StartInstanceLocation;
  };
}

#endif