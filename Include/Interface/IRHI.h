#pragma once

#include "../KTL/SharedPtr.hpp"
#include "../Math/kGeometry.hpp"
#include "RHIStructs.h"
#include "ICrossShaderCompiler.h"

K3D_COMMON_NS
{
struct IObject
{
  virtual void Release() = 0;
};
struct ICommandQueue;
typedef ::k3d::SharedPtr<ICommandQueue> CommandQueueRef;
struct ICommandBuffer;
typedef ::k3d::SharedPtr<ICommandBuffer> CommandBufferRef;
struct IRenderCommandEncoder;
typedef ::k3d::SharedPtr<IRenderCommandEncoder> RenderCommandEncoderRef;
struct IComputeCommandEncoder;
typedef ::k3d::SharedPtr<IComputeCommandEncoder> ComputeCommandEncoderRef;
struct IParallelRenderCommandEncoder;
typedef ::k3d::SharedPtr<IParallelRenderCommandEncoder>
  ParallelRenderCommandEncoderRef;

struct IDevice;
typedef ::k3d::SharedPtr<IDevice> DeviceRef;
struct IFactory;
typedef ::k3d::SharedPtr<IFactory> FactoryRef;
struct IGpuResource;
typedef ::k3d::SharedPtr<IGpuResource> GpuResourceRef;
struct ITexture;
typedef ::k3d::SharedPtr<ITexture> TextureRef;
struct IBuffer;
typedef ::k3d::SharedPtr<IBuffer> BufferRef;
struct ITextureView;
typedef ::k3d::SharedPtr<ITextureView> TextureViewRef;
struct IBufferView;
typedef ::k3d::SharedPtr<IBufferView> BufferViewRef;
struct IShaderResourceView;
typedef ::k3d::SharedPtr<IShaderResourceView> ShaderResourceViewRef;
struct IPipelineState;
typedef ::k3d::SharedPtr<IPipelineState> PipelineStateRef;
struct IRenderPipelineState;
typedef ::k3d::SharedPtr<IRenderPipelineState> RenderPipelineStateRef;
struct IComputePipelineState;
typedef ::k3d::SharedPtr<IComputePipelineState> ComputePipelineStateRef;
struct IRenderPipelineEncoder;
typedef ::k3d::SharedPtr<IRenderPipelineEncoder> RenderPipelineEncoderRef;
struct IComputePipelineEncoder;
typedef ::k3d::SharedPtr<IComputePipelineEncoder> ComputePipelineEncoderRef;
struct ISwapChain;
typedef ::k3d::SharedPtr<ISwapChain> SwapChainRef;
struct ISampler;
typedef ::k3d::SharedPtr<ISampler> SamplerRef;
typedef const SamplerRef SamplerCRef;
struct IDescriptor;
typedef ::k3d::SharedPtr<IDescriptor> DescriptorRef;
struct IRenderTarget;
typedef ::k3d::SharedPtr<IRenderTarget> RenderTargetRef;
struct IRenderPass;
typedef ::k3d::SharedPtr<IRenderPass> RenderPassRef;
/**
 * Semaphore used for GPU to GPU syncs,
 * Specifically used to sync queue submissions (on the same or different
 * queues), Set on GPU, Wait on GPU (inter-queue) Events: Set anywhere, Wait on
 * GPU (intra-queue)
 */
struct ISemaphore;

/**
 * @see
 * https://www.reddit.com/r/vulkan/comments/47tc3s/differences_between_vkfence_vkevent_and/?
 * Fence is GPU to CPU syncs
 * Set on GPU, Wait on CPU
 */
struct ISyncFence;
typedef ::k3d::SharedPtr<ISyncFence> SyncFenceRef;

/**
 * Vulkan has pipeline layout,
 * D3D12 is root signature.
 * PipelineLayout defines shaders impose constraints on table layout.
 */
struct IPipelineLayout;
typedef ::k3d::SharedPtr<IPipelineLayout> PipelineLayoutRef;

struct IGpuResource
{
  virtual ~IGpuResource() {}
  virtual void* Map(uint64 start, uint64 size) = 0;
  virtual void UnMap() = 0;

  virtual uint64 GetLocation() const { return 0; }
  virtual ResourceDesc GetDesc() const = 0;

  /**
   * Vulkan: texture uses image layout as resource state
   * D3D12: used for transition, maybe used as ShaderVisiblity determination in
   * STATIC_SAMPLER and descriptor table
   */
  virtual EResourceState GetState() const { return ERS_Unknown; }
  virtual uint64 GetSize() const = 0;
};

struct ISampler
{
  virtual SamplerState GetSamplerDesc() const = 0;
  virtual ~ISampler() {}
};

struct IShaderResourceView
{
  virtual ~IShaderResourceView() {}
  virtual GpuResourceRef GetResource() const = 0;
  virtual ResourceViewDesc GetDesc() const = 0;
};

struct ITexture : public IGpuResource
{
  virtual ~ITexture() {}
  virtual SamplerCRef GetSampler() const = 0;
  virtual void BindSampler(SamplerRef) = 0;
  virtual void SetResourceView(ShaderResourceViewRef) = 0;
  virtual ShaderResourceViewRef GetResourceView() const = 0;
};

struct IBuffer : public IGpuResource
{
  virtual ~IBuffer() {}
};

struct IDescriptor
{
  virtual void Update(uint32 bindSet, GpuResourceRef) = 0;
  virtual void Update(uint32 bindSet, SamplerRef){};
  virtual uint32 GetSlotNum() const { return 0; }
  virtual ~IDescriptor() {}
};

struct IDeviceAdapter
{
  virtual DeviceRef GetDevice() = 0;
  virtual ~IDeviceAdapter() {}
};

/**
 * Vulkan has pipeline layout,
 * D3D12 is root signature.
 * PipelineLayout defines shaders impose constraints on table layout.
 */
struct IPipelineLayout
{
  virtual DescriptorRef GetDescriptorSet() const = 0;
  virtual ~IPipelineLayout() {}
};

/**
 * @see
 * https://www.reddit.com/r/vulkan/comments/47tc3s/differences_between_vkfence_vkevent_and/?
 * Fence is GPU to CPU syncs
 * Set on GPU, Wait on CPU
 */
struct ISyncFence
{
  virtual void Signal(int32 fenceVal) = 0;
  virtual void Reset() {}
  virtual void WaitFor(uint64 time) = 0;
  virtual ~ISyncFence() {}
};

struct IRenderTarget
{
  virtual ~IRenderTarget() {}
  virtual void SetClearColor(kMath::Vec4f clrColor) = 0;
  virtual void SetClearDepthStencil(float depth, uint32 stencil) = 0;
  virtual GpuResourceRef GetBackBuffer() = 0;
};

struct ComputePipelineStateDesc
{
  ShaderBundle ComputeShader;
};

struct IPipelineState
{
  virtual ~IPipelineState() {}
  virtual EPipelineType GetType() const = 0;

  // rebuild pipeline state if is dirty
  virtual void Rebuild() = 0;

  // storage options
  virtual void SavePSO(::k3d::String const& Path) {}
  virtual void LoadPSO(::k3d::String const& Path) {}
};

struct IRenderPipelineState : public IPipelineState
{
  virtual ~IRenderPipelineState() {}

  virtual void SetRasterizerState(const RasterizerState&) = 0;
  virtual void SetBlendState(const BlendState&) = 0;
  virtual void SetDepthStencilState(const DepthStencilState&) = 0;
  virtual void SetPrimitiveTopology(const EPrimitiveType) = 0;
  virtual void SetRenderTargetFormat(const RenderTargetFormat&) = 0;
  virtual void SetSampler(SamplerRef) = 0;
};

struct IComputePipelineState : public IPipelineState
{
  virtual ~IComputePipelineState() {}
};

using PipelineLayoutDesc = shc::BindingTable;

struct AttachmentDesc
{
  AttachmentDesc()
    : LoadAction(ELA_Clear)
    , StoreAction(ESA_Store)
  {
  }
  ELoadAction LoadAction;
  EStoreAction StoreAction;
  TextureRef pTexture;
};

struct ColorAttachmentDesc : public AttachmentDesc
{
  kMath::Vec4f ClearColor;
};

using ColorAttachmentArray = k3d::DynArray<ColorAttachmentDesc>;

struct DepthAttachmentDesc : public AttachmentDesc
{
  float ClearDepth;
};

struct StencilAttachmentDesc : public AttachmentDesc
{
  float ClearStencil;
};

struct RenderPassDesc
{
  ColorAttachmentArray ColorAttachments;
  SharedPtr<DepthAttachmentDesc> pDepthAttachment;
  SharedPtr<StencilAttachmentDesc> pStencilAttachment;
};

struct IRenderPass : public IObject
{
  virtual RenderPassDesc GetDesc() const = 0;
};

struct IDevice : public IObject
{
  enum Result
  {
    DeviceNotFound,
    DeviceFound
  };

  virtual ~IDevice() {}

  virtual GpuResourceRef NewGpuResource(ResourceDesc const&) = 0;
  virtual ShaderResourceViewRef NewShaderResourceView(
    GpuResourceRef,
    ResourceViewDesc const&) = 0;
  virtual SamplerRef NewSampler(const SamplerState&) = 0;

  virtual PipelineLayoutRef NewPipelineLayout(
    PipelineLayoutDesc const& table) = 0;
  virtual SyncFenceRef CreateFence() = 0;

//  virtual RenderTargetRef NewRenderTarget(RenderTargetLayout const&) = 0;

  virtual RenderPassRef CreateRenderPass(RenderPassDesc const&) = 0;

  virtual PipelineStateRef CreateRenderPipelineState(
    RenderPipelineStateDesc const&,
    PipelineLayoutRef,
    RenderPassRef) = 0;

  virtual PipelineStateRef CreateComputePipelineState(
    ComputePipelineStateDesc const&,
    PipelineLayoutRef) = 0;

  virtual CommandQueueRef CreateCommandQueue(ECommandType const&) = 0;

  virtual void WaitIdle() {}

  /* equal with d3d12's getcopyfootprint or vulkan's getImagesubreslayout.
   */
  virtual void QueryTextureSubResourceLayout(TextureRef,
                                             TextureResourceSpec const& spec,
                                             SubResourceLayout*)
  {
  }
};

struct IFactory : public IObject
{
  virtual void EnumDevices(k3d::DynArray<DeviceRef>& Devices) = 0;
  virtual SwapChainRef CreateSwapchain(CommandQueueRef pCommandQueue,
                                       void* nPtr,
                                       SwapChainDesc&) = 0;
};

struct ISwapChain : public IObject
{
  virtual void Resize(uint32 Width, uint32 Height) = 0;
  virtual void Present() = 0;
  virtual TextureRef GetCurrentTexture() = 0;
};

struct TextureCopyLocation
{
  typedef ::k3d::DynArray<uint32> ResIds;
  typedef ::k3d::DynArray<PlacedSubResourceFootprint> ResFootprints;

  enum ESubResource
  {
    ESubResourceIndex,
    ESubResourceFootPrints
  };

  TextureCopyLocation(GpuResourceRef ptrResource, ResIds subResourceIndex)
    : pResource(ptrResource)
    , SubResourceIndexes(subResourceIndex)
  {
  }

  TextureCopyLocation(GpuResourceRef ptrResource, ResFootprints footprints)
    : pResource(ptrResource)
    , SubResourceFootPrints(footprints)
  {
  }

  TextureCopyLocation() = default;

  ~TextureCopyLocation() {}

  GpuResourceRef pResource;
  ESubResource SubResourceType = ESubResourceIndex;
  ResIds SubResourceIndexes;
  ResFootprints SubResourceFootPrints;
};

struct ICommandQueue
{
  virtual CommandBufferRef ObtainCommandBuffer(ECommandUsageType const&) = 0;
};

struct ICommandBuffer : public IObject
{
  virtual void Commit(SyncFenceRef pFence = nullptr) = 0;
  // For Vulkan, This command will be appended to the tail
  virtual void Present(SwapChainRef pSwapChain, SyncFenceRef pFence) = 0;
  virtual void Reset() = 0;
  virtual RenderCommandEncoderRef RenderCommandEncoder(
    RenderPassDesc const&) = 0;
  virtual ComputeCommandEncoderRef ComputeCommandEncoder(
    ComputePipelineStateRef) = 0;
  virtual ParallelRenderCommandEncoderRef ParallelRenderCommandEncoder(
    RenderPassDesc const&) = 0;
  // blit
  virtual void CopyTexture(const TextureCopyLocation& Dest,
                           const TextureCopyLocation& Src) = 0;
  //
  virtual void CopyBuffer(GpuResourceRef Dest,
                          GpuResourceRef Src,
                          CopyBufferRegion const& Region) = 0;
  virtual void Transition(GpuResourceRef pResource,
                          EResourceState const&
                            State /*, rhi::EPipelineStage const& Stage*/) = 0;
};

struct ICommandEncoder
{
  virtual void SetPipelineState(uint32 HashCode, PipelineStateRef const&) = 0;
  virtual void SetPipelineLayout(PipelineLayoutRef const&) = 0;
  virtual void EndEncode() = 0;
};

struct IRenderCommandEncoder : public ICommandEncoder
{
  virtual void SetScissorRect(const Rect&) = 0;
  virtual void SetViewport(const ViewportDesc&) = 0;
  virtual void SetIndexBuffer(const IndexBufferView& IBView) = 0;
  virtual void SetVertexBuffer(uint32 Slot, const VertexBufferView& VBView) = 0;
  virtual void SetPrimitiveType(EPrimitiveType) = 0;
  virtual void DrawInstanced(DrawInstancedParam) = 0;
  virtual void DrawIndexedInstanced(DrawIndexedInstancedParam) = 0;
};

struct IComputeCommandEncoder : public ICommandEncoder
{
  virtual void Dispatch(uint32 GroupCountX,
                        uint32 GroupCountY,
                        uint32 GroupCountZ) = 0;
};
// Begin with a single rendering pass, encoded from multiple threads
// simultaneously.
struct IParallelRenderCommandEncoder : public ICommandEncoder
{
  virtual RenderCommandEncoderRef SubRenderCommandEncoder() = 0;
};

}
