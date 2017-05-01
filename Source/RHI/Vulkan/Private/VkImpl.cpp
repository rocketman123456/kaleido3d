#include "VkCommon.h"
#include "Public/IVkRHI.h"
#include "VkConfig.h"
#include "VkEnums.h"
#include "VkRHI.h"
#include <RHI/RHIUtil.h>
#include <Core/Module.h>
#include <Core/Os.h>
#include <Core/Utils/farmhash.h>
#include <Core/WebSocket.h>
#include <Log/Public/ILogModule.h>
#include <cstdarg>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

using namespace k3d::shc;
using namespace std;

PFN_vklogCallBack g_logCallBack = nullptr;

static thread_local char logContent[2048] = { 0 };
#define LOG_SERVER 1

#if K3DPLATFORM_OS_ANDROID
#define OutputDebugStringA(str)                                                \
  __android_log_print(ANDROID_LOG_DEBUG, "VkRHI", "%s", str);
#endif

void
VkLog(k3d::ELogLevel const& Lv, const char* tag, const char* fmt, ...)
{
  if (g_logCallBack != nullptr) {
    va_list va;
    va_start(va, fmt);
    g_logCallBack(Lv, tag, fmt, va);
    va_end(va);
  } else {
#if LOG_SERVER
    va_list va;
    va_start(va, fmt);
    vsprintf(logContent, fmt, va);
    va_end(va);

    auto logModule = k3d::StaticPointerCast<k3d::ILogModule>(
      k3d::GlobalModuleManager.FindModule("KawaLog"));
    if (logModule) {
      k3d::ILogger* logger = logModule->GetLogger(k3d::ELoggerType::EWebsocket);
      if (logger) {
        logger->Log(Lv, tag, logContent);
      }

      if (Lv >= k3d::ELogLevel::Debug) {
        logger = logModule->GetLogger(k3d::ELoggerType::EConsole);
        if (logger) {
          logger->Log(Lv, tag, logContent);
        }
      }

      if (Lv >= k3d::ELogLevel::Warn) {
        logger = logModule->GetLogger(k3d::ELoggerType::EFile);
        if (logger) {
          logger->Log(Lv, tag, logContent);
        }
      }
    }
#else
    va_list va;
    va_start(va, fmt);
    vsprintf(logContent, fmt, va);
    va_end(va);
    OutputDebugStringA(logContent);
#endif
  }
}

void
SetVkLogCallback(PFN_vklogCallBack func)
{
  g_logCallBack = func;
}

K3D_VK_BEGIN

MapFramebuffer DeviceObjectCache::s_Framebuffer;
MapRenderpass DeviceObjectCache::s_RenderPass;

PtrCmdAlloc
CommandAllocator::CreateAllocator(uint32 queueFamilyIndex,
                                  bool transient,
                                  Device::Ptr device)
{
  PtrCmdAlloc result =
    PtrCmdAlloc(new CommandAllocator(queueFamilyIndex, transient, device));
  return result;
}

CommandAllocator::~CommandAllocator()
{
  Destroy();
}

void
CommandAllocator::Initialize()
{
  VkCommandPoolCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  createInfo.pNext = nullptr;
  createInfo.queueFamilyIndex = m_FamilyIndex;
  createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (m_Transient) {
    createInfo.flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  }

  K3D_VK_VERIFY(
    vkCreateCommandPool(GetRawDevice(), &createInfo, nullptr, &m_Pool));
}

void
CommandAllocator::Destroy()
{
  if (VK_NULL_HANDLE == m_Pool || !GetRawDevice()) {
    return;
  }
  VKLOG(Info,
        "CommandAllocator destroy.. -- %d. (device:0x%0x)",
        m_Pool,
        GetRawDevice());
  vkDestroyCommandPool(GetRawDevice(), m_Pool, nullptr);
  m_Pool = VK_NULL_HANDLE;
}

CommandAllocator::CommandAllocator(uint32 queueFamilyIndex,
                                   bool transient,
                                   Device::Ptr device)
  : m_FamilyIndex(queueFamilyIndex)
  , m_Transient(transient)
  , DeviceChild(device)
{
  Initialize();
}

CommandQueue::CommandQueue(Device::Ptr pDevice,
                           VkQueueFlags queueTypes,
                           uint32 queueFamilyIndex,
                           uint32 queueIndex)
  : CommandQueue::This(pDevice)
{
  Initialize(queueTypes, queueFamilyIndex, queueIndex);
}

CommandQueue::~CommandQueue()
{
  Destroy();
}

k3d::CommandBufferRef
CommandQueue::ObtainCommandBuffer(k3d::ECommandUsageType const& Usage)
{
  VkCommandBuffer Cmd = m_ReUsableCmdBufferPool->RequestCommandBuffer();
  m_ReUsableCmdBufferPool->BeginFrame();
  return MakeShared<CommandBuffer>(m_Device, SharedFromThis(), Cmd);
}

void
CommandQueue::Submit(const std::vector<VkCommandBuffer>& cmdBufs,
                     const std::vector<VkSemaphore>& waitSemaphores,
                     const std::vector<VkPipelineStageFlags>& waitStageMasks,
                     VkFence fence,
                     const std::vector<VkSemaphore>& signalSemaphores)
{
  const VkPipelineStageFlags* pWaitDstStageMask =
    (waitSemaphores.size() == waitStageMasks.size()) ? waitStageMasks.data()
                                                     : nullptr;
  VkSubmitInfo submits = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
  submits.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
  submits.pWaitSemaphores =
    waitSemaphores.empty() ? nullptr : waitSemaphores.data();
  submits.pWaitDstStageMask = pWaitDstStageMask;
  submits.commandBufferCount = static_cast<uint32_t>(cmdBufs.size());
  submits.pCommandBuffers = cmdBufs.empty() ? nullptr : cmdBufs.data();
  submits.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
  submits.pSignalSemaphores =
    signalSemaphores.empty() ? nullptr : signalSemaphores.data();
  this->Submit({ submits }, fence);
}

VkResult
CommandQueue::Submit(const std::vector<VkSubmitInfo>& submits, VkFence fence)
{
  K3D_ASSERT(!submits.empty());
  uint32_t submitCount = static_cast<uint32_t>(submits.size());
  const VkSubmitInfo* pSubmits = submits.data();
  VkResult err = vkQueueSubmit(m_NativeObj, submitCount, pSubmits, fence);
  K3D_ASSERT((err == VK_SUCCESS) || (err == VK_ERROR_DEVICE_LOST));
  return err;
}

void
CommandQueue::Present(SpSwapChain& pSwapChain)
{
  VkResult PresentResult = VK_SUCCESS;
  VkPresentInfoKHR PresentInfo = {
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    nullptr,
    1,                               // waitSemaphoreCount
    &pSwapChain->m_smpPresent,       // pWaitSemaphores
    1,                               // SwapChainCount
    &pSwapChain->m_SwapChain,
    &pSwapChain->m_CurrentBufferID, // SwapChain Image Index
    &PresentResult
  };
  VkResult Ret = vkQueuePresentKHR(NativeHandle(), &PresentInfo);
  VKLOG(Info, "CommandQueue :: Present, waiting on 0x%0x.", pSwapChain->m_smpPresent);
  K3D_VK_VERIFY(Ret);
  K3D_VK_VERIFY(PresentResult);
  //vkQueueWaitIdle(NativeHandle());
}

void
CommandQueue::WaitIdle()
{
  K3D_VK_VERIFY(vkQueueWaitIdle(m_NativeObj));
}

void
CommandQueue::Initialize(VkQueueFlags queueTypes,
                         uint32 queueFamilyIndex,
                         uint32 queueIndex)
{
  m_QueueFamilyIndex = queueFamilyIndex;
  m_QueueIndex = queueIndex;
  vkGetDeviceQueue(
    m_Device->GetRawDevice(), m_QueueFamilyIndex, m_QueueIndex, &m_NativeObj);
  m_ReUsableCmdBufferPool = MakeShared<CommandBufferManager>(
    m_Device->GetRawDevice(), VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_QueueIndex);
}

void
CommandQueue::Destroy()
{
  if (m_NativeObj == VK_NULL_HANDLE)
    return;
  m_NativeObj = VK_NULL_HANDLE;
}

void
CommandBuffer::Release()
{
}

void
CommandBuffer::Commit(SyncFenceRef pFence)
{
  if (!m_Ended) {
    // when in presenting, check if present image is in `Present State`
    // if not, make state transition
    if (m_PendingSwapChain) {
      /*auto PresentImage = m_PendingSwapChain->GetCurrentTexture();
      auto ImageState = PresentImage->GetState();
      if (ImageState != k3d::ERS_Present) {
        Transition(PresentImage, k3d::ERS_Present);
      }*/
    }

    auto Ret = vkEndCommandBuffer(m_NativeObj);
    K3D_VK_VERIFY(Ret);
    m_Ended = true;
  }

  if (pFence)
  {
    pFence->Reset();
  }

  // Submit First
  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  if (m_PendingSwapChain)
  {
    VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto vSwapchain = StaticPointerCast<SwapChain>(m_PendingSwapChain);    
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &vSwapchain->m_smpRenderFinish;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &vSwapchain->m_smpPresent;
    submitInfo.pWaitDstStageMask = &stageFlags;
    VKLOG(Info, "Command Buffer :: Commit wait on 0x%0x, signal on 0x%0x.",
      vSwapchain->m_smpRenderFinish, vSwapchain->m_smpPresent);
  }
  else 
  {
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
  }
  
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &m_NativeObj;
  auto vFence = StaticPointerCast<Fence>(pFence);
  auto Ret = m_OwningQueue->Submit({ submitInfo }, vFence ?
    vFence->NativeHandle() : VK_NULL_HANDLE);
  K3D_VK_VERIFY(Ret);

  if (pFence)
  {
    pFence->WaitFor(10000);
  }

  m_OwningQueue->WaitIdle();

  // Do Present
  if (m_PendingSwapChain) {
    auto pSwapChain = StaticPointerCast<SwapChain>(m_PendingSwapChain);
    m_OwningQueue->Present(pSwapChain);
    // After Present, Prepare For Next Frame
    pSwapChain->AcquireNextImage();
    m_PendingSwapChain.Reset();
  }
}

void
CommandBuffer::Present(k3d::SwapChainRef pSwapChain, k3d::SyncFenceRef pFence)
{
  m_PendingSwapChain = pSwapChain;
}

void
CommandBuffer::Reset()
{
  vkResetCommandBuffer(m_NativeObj, 0);
}

k3d::RenderCommandEncoderRef
CommandBuffer::RenderCommandEncoder(k3d::RenderPassDesc const& Desc)
{
  auto pRenderpass = StaticPointerCast<RenderPass>(m_Device->CreateRenderPass(Desc));
  auto pFramebuffer = m_Device->GetOrCreateFramebuffer(Desc);
  pRenderpass->Begin(m_NativeObj, pFramebuffer);
  return MakeShared<vk::RenderCommandEncoder>(SharedFromThis(),
                                              ECmdLevel::Primary);
}

k3d::ComputeCommandEncoderRef
CommandBuffer::ComputeCommandEncoder(k3d::ComputePipelineStateRef)
{
  return k3d::ComputeCommandEncoderRef();
}

k3d::ParallelRenderCommandEncoderRef
CommandBuffer::ParallelRenderCommandEncoder(k3d::RenderPassDesc const& Desc)
{
  return k3d::ParallelRenderCommandEncoderRef();
}

void
CommandBuffer::CopyTexture(const k3d::TextureCopyLocation& Dest,
                           const k3d::TextureCopyLocation& Src)
{
  K3D_ASSERT(Dest.pResource && Src.pResource);
  if (Src.pResource->GetDesc().Type == k3d::EGT_Buffer &&
      Dest.pResource->GetDesc().Type != k3d::EGT_Buffer) {
    DynArray<VkBufferImageCopy> Copies;
    for (auto footprint : Src.SubResourceFootPrints) {
      VkBufferImageCopy bImgCpy = {};
      bImgCpy.bufferOffset = footprint.BufferOffSet;
      bImgCpy.imageOffset = { footprint.TOffSetX,
                              footprint.TOffSetY,
                              footprint.TOffSetZ };
      bImgCpy.imageExtent = { footprint.Footprint.Dimension.Width,
                              footprint.Footprint.Dimension.Height,
                              footprint.Footprint.Dimension.Depth };
      bImgCpy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      Copies.Append(bImgCpy);
    }
    auto SrcBuf = StaticPointerCast<Buffer>(Src.pResource);
    auto DestImage = StaticPointerCast<Texture>(Dest.pResource);
    vkCmdCopyBufferToImage(m_NativeObj,
                           SrcBuf->NativeHandle(),
                           DestImage->NativeHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           Copies.Count(),
                           Copies.Data());
  }
}

void
CommandBuffer::CopyBuffer(k3d::GpuResourceRef Dest,
                          k3d::GpuResourceRef Src,
                          k3d::CopyBufferRegion const& Region)
{
  K3D_ASSERT(Dest && Src);
  K3D_ASSERT(Dest->GetDesc().Type == k3d::EGT_Buffer &&
             Src->GetDesc().Type == k3d::EGT_Buffer);
  auto DestBuf = StaticPointerCast<Buffer>(Dest);
  auto SrcBuf = StaticPointerCast<Buffer>(Src);
  vkCmdCopyBuffer(m_NativeObj,
                  SrcBuf->NativeHandle(),
                  DestBuf->NativeHandle(),
                  1,
                  (const VkBufferCopy*)&Region);
}

void InferImageBarrierFromDesc(
  SpTexture pTexture,
  EResourceState const& DestState,
  VkImageMemoryBarrier& Barrier)
{
  auto Desc = pTexture->GetDesc();
  if (Desc.ViewType == k3d::EGVT_RTV)
  {
    VkImageLayout Src = pTexture->GetImageLayout();
    switch (Src)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      Barrier.srcAccessMask = 0;
      break;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
      Barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      Barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      break;
    default:
      break;
    }
    
    VkImageLayout Dest = g_ResourceState[DestState];
    switch(Dest)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      Barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      Barrier.dstAccessMask = Barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      if (Barrier.srcAccessMask == 0)
      {
        Barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      }
      Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      Barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      break;
    default:
      break;
    }
  }
}

// TODO:
void
CommandBuffer::Transition(k3d::GpuResourceRef pResource,
                          k3d::EResourceState const& State)
{
  auto Desc = pResource->GetDesc();
  if (Desc.Type == k3d::EGT_Buffer) // Buffer Barrier
  {
    // buffer offset, size
    VkBufferMemoryBarrier BufferBarrier = {
      VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
    };
  } else // ImageLayout Transition
  {
    //if (pResource->GetState() == State)
    //  return;
    auto pTexture = StaticPointerCast<Texture>(pResource);
    VkImageLayout SrcLayout = g_ResourceState[pTexture->GetState()];
    VkImageLayout DestLayout = g_ResourceState[State];
    VkImageMemoryBarrier ImageBarrier = {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      nullptr,
      0, //srcAccessMask
      VK_ACCESS_MEMORY_READ_BIT, //dstAccessMask
      SrcLayout,
      DestLayout,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      pTexture->NativeHandle(),
      pTexture->GetSubResourceRange()
    };
    InferImageBarrierFromDesc(pTexture, State, ImageBarrier);
    VkPipelineStageFlagBits SrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlagBits DestStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (DestLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
      SrcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (DestLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
      DestStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    }
    vkCmdPipelineBarrier(m_NativeObj,
                         SrcStage,
                         DestStage,
                         0, // DependencyFlag
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &ImageBarrier);
    pTexture->m_UsageState = State;
    pTexture->m_ImageLayout = DestLayout;
  }
}

CommandBuffer::CommandBuffer(Device::Ptr pDevice,
                             SpCmdQueue pQueue,
                             VkCommandBuffer Buffer)
  : CommandBuffer::This(pDevice)
  , m_OwningQueue(pQueue)
  , m_Ended(false)
{
  m_NativeObj = Buffer;
  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.pNext = nullptr;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  beginInfo.pInheritanceInfo = nullptr;
  K3D_VK_VERIFY(vkBeginCommandBuffer(m_NativeObj, &beginInfo));
}

/**
 * @see #RHIRect2VkRect2D
 */
void
RHIRect2VkRect(const k3d::Rect& rect, VkRect2D& r2d)
{
  r2d.extent.width = rect.Right - rect.Left;
  r2d.extent.height = rect.Bottom - rect.Top;
  r2d.offset.x = rect.Left;
  r2d.offset.y = rect.Top;
}

void
RenderCommandEncoder::SetScissorRect(const k3d::Rect& pRect)
{
  VkRect2D Rect2D;
  RHIRect2VkRect(pRect, Rect2D);
  vkCmdSetScissor(m_MasterCmd->NativeHandle(), 0, 1, &Rect2D);
}

void
RenderCommandEncoder::SetViewport(const k3d::ViewportDesc& viewport)
{
  vkCmdSetViewport(m_MasterCmd->NativeHandle(),
                   0,
                   1,
                   reinterpret_cast<const VkViewport*>(&viewport));
}

void
RenderCommandEncoder::SetIndexBuffer(const k3d::IndexBufferView& IBView)
{
  VkBuffer buf = (VkBuffer)(IBView.BufferLocation);
  vkCmdBindIndexBuffer(
    m_MasterCmd->NativeHandle(), buf, 0, VK_INDEX_TYPE_UINT32);
}

void
RenderCommandEncoder::SetVertexBuffer(uint32 Slot,
                                      const k3d::VertexBufferView& VBView)
{
  VkBuffer buf = (VkBuffer)(VBView.BufferLocation);
  VkDeviceSize offsets[1] = { 0 };
  vkCmdBindVertexBuffers(m_MasterCmd->NativeHandle(), Slot, 1, &buf, offsets);
}

void RenderCommandEncoder::SetPrimitiveType(k3d::EPrimitiveType)
{
}

void
RenderCommandEncoder::DrawInstanced(k3d::DrawInstancedParam drawParam)
{
  vkCmdDraw(m_MasterCmd->NativeHandle(),
            drawParam.VertexCountPerInstance,
            drawParam.InstanceCount,
            drawParam.StartVertexLocation,
            drawParam.StartInstanceLocation);
}

void
RenderCommandEncoder::DrawIndexedInstanced(
  k3d::DrawIndexedInstancedParam drawParam)
{
  vkCmdDrawIndexed(m_MasterCmd->NativeHandle(),
                   drawParam.IndexCountPerInstance,
                   drawParam.InstanceCount,
                   drawParam.StartIndexLocation,
                   drawParam.BaseVertexLocation,
                   drawParam.StartInstanceLocation);
}

void
RenderCommandEncoder::EndEncode()
{
  if (m_Level != ECmdLevel::Secondary) {
    vkCmdEndRenderPass(m_MasterCmd->NativeHandle());
  }
  //This::EndEncode();
}

// from primary render command buffer
RenderCommandEncoder::RenderCommandEncoder(SpCmdBuffer pCmd, ECmdLevel Level)
  : RenderCommandEncoder::This(pCmd)
  , m_Level(Level)
{
}

RenderCommandEncoder::RenderCommandEncoder(SpParallelCmdEncoder ParentEncoder,
                                           SpCmdBuffer pCmd)
  : RenderCommandEncoder::This(pCmd)
  , m_Level(ECmdLevel::Secondary)
{
  // render pass begun before allocate
}

void
ComputeCommandEncoder::Dispatch(uint32 GroupCountX,
                                uint32 GroupCountY,
                                uint32 GroupCountZ)
{
  vkCmdDispatch(
    m_MasterCmd->NativeHandle(), GroupCountX, GroupCountY, GroupCountZ);
}

k3d::RenderCommandEncoderRef
ParallelCommandEncoder::SubRenderCommandEncoder()
{
  if (!m_RenderpassBegun) {
    VkRenderPassBeginInfo renderPassBeginInfo = {
      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
    };
    /*renderPassBeginInfo.renderPass = pRT->GetRenderpass();
    renderPassBeginInfo.renderArea = pRT->GetRenderArea();*/
    // renderPassBeginInfo.pClearValues = pRT->m_ClearValues;
    renderPassBeginInfo.clearValueCount = 2;
    // renderPassBeginInfo.framebuffer = pRT->GetFramebuffer();
    vkCmdBeginRenderPass(m_MasterCmd->NativeHandle(),
                         &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    m_RenderpassBegun = true;
  }
  // allocate secondary cmd

  return nullptr;
}

void
ParallelCommandEncoder::EndEncode()
{
  DynArray<VkCommandBuffer> Cmds;
  for (auto pCmd : m_RecordedCmds) {
    Cmds.Append(pCmd->OwningCmd());
  }
  vkCmdExecuteCommands(m_MasterCmd->NativeHandle(), Cmds.Count(), Cmds.Data());

  // end renderpass
  if (m_RenderpassBegun) {
    vkCmdEndRenderPass(m_MasterCmd->NativeHandle());
  }

  This::EndEncode();
}

RenderTarget::RenderTarget(Device::Ptr pDevice,
                           Texture::TextureRef texture,
                           SpFramebuffer framebuffer,
                           VkRenderPass renderpass)
  : DeviceChild(pDevice)
  , m_Renderpass(renderpass)
{
  m_AcquireSemaphore = PtrSemaphore(new Semaphore(pDevice));
}

RenderTarget::~RenderTarget()
{
}

VkFramebuffer
RenderTarget::GetFramebuffer() const
{
  return m_Framebuffer;
}

VkRenderPass
RenderTarget::GetRenderpass() const
{
  return m_Renderpass;
}

Texture::TextureRef
RenderTarget::GetTexture() const
{
  return nullptr;
}

VkRect2D
RenderTarget::GetRenderArea() const
{
  VkRect2D renderArea = {};
  renderArea.offset = { 0, 0 };
  renderArea.extent = { /*m_Framebuffer->GetWidth(), m_Framebuffer->GetHeight()*/ };
  return renderArea;
}

k3d::GpuResourceRef
RenderTarget::GetBackBuffer()
{
  return nullptr;
}

RenderPipelineState::RenderPipelineState(
  Device::Ptr pDevice,
  k3d::RenderPipelineStateDesc const& desc, 
  k3d::PipelineLayoutRef pPipelineLayout,
  k3d::RenderPassRef pRenderPass)
  : TPipelineState<k3d::IRenderPipelineState>(pDevice)
  , m_RenderPass(VK_NULL_HANDLE)
  , m_GfxCreateInfo{}
  , m_WeakRenderPassRef(pRenderPass)
  , m_weakPipelineLayoutRef(pPipelineLayout)
{
  memset(&m_GfxCreateInfo, 0, sizeof(m_GfxCreateInfo));
  InitWithDesc(desc);
}

RenderPipelineState::~RenderPipelineState()
{
  Destroy();
}

void
RenderPipelineState::BindRenderPass(VkRenderPass RenderPass)
{
  this->m_RenderPass = RenderPass;
}

void
RenderPipelineState::Rebuild()
{
  if (VK_NULL_HANDLE != m_Pipeline)
    return;
  VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
  pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  K3D_VK_VERIFY(vkCreatePipelineCache(m_Device->GetRawDevice(),
                                      &pipelineCacheCreateInfo,
                                      nullptr,
                                      &m_PipelineCache));
  K3D_VK_VERIFY(vkCreateGraphicsPipelines(m_Device->GetRawDevice(),
                                          m_PipelineCache,
                                          1,
                                          &m_GfxCreateInfo,
                                          nullptr,
                                          &m_Pipeline));
}

#if 0
void
RenderPipelineState::SavePSO(const char* path)
{
  size_t szPSO = 0;
  K3D_VK_VERIFY(
    vkGetPipelineCacheData(GetRawDevice(), m_PipelineCache, &szPSO, nullptr));
  if (!szPSO || !path)
    return;
  DynArray<char> dataBlob;
  dataBlob.Resize(szPSO);
  vkGetPipelineCacheData(
    GetRawDevice(), m_PipelineCache, &szPSO, dataBlob.Data());
  Os::File psoCacheFile(path);
  psoCacheFile.Open(IOWrite);
  psoCacheFile.Write(dataBlob.Data(), szPSO);
  psoCacheFile.Close();
}

void
RenderPipelineState::LoadPSO(const char* path)
{
  Os::MemMapFile psoFile;
  if (!path || !psoFile.Open(path, IORead))
    return;
  VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
  pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  pipelineCacheCreateInfo.pInitialData = psoFile.FileData();
  pipelineCacheCreateInfo.initialDataSize = psoFile.GetSize();
  K3D_VK_VERIFY(vkCreatePipelineCache(
    GetRawDevice(), &pipelineCacheCreateInfo, nullptr, &m_PipelineCache));
}
#endif
void
RenderPipelineState::SetRasterizerState(const k3d::RasterizerState& rasterState)
{
  m_RasterizationState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  // Solid polygon mode
  m_RasterizationState.polygonMode = g_FillMode[rasterState.FillMode];
  // No culling
  m_RasterizationState.cullMode = g_CullMode[rasterState.CullMode];
  m_RasterizationState.frontFace = rasterState.FrontCCW
                                     ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                     : VK_FRONT_FACE_CLOCKWISE;
  m_RasterizationState.depthClampEnable =
    rasterState.DepthClipEnable ? VK_TRUE : VK_FALSE;
  m_RasterizationState.rasterizerDiscardEnable = VK_FALSE;
  m_RasterizationState.depthBiasEnable = VK_FALSE;
  this->m_GfxCreateInfo.pRasterizationState = &m_RasterizationState;
}

// One blend attachment state
// Blending is not used in this example
// TODO
void
RenderPipelineState::SetBlendState(const k3d::BlendState& blendState)
{
  m_ColorBlendState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
  blendAttachmentState[0].colorWriteMask = 0xf;
  blendAttachmentState[0].blendEnable = blendState.Enable ? VK_TRUE : VK_FALSE;
  m_ColorBlendState.attachmentCount = 1;
  m_ColorBlendState.pAttachments = blendAttachmentState;
  this->m_GfxCreateInfo.pColorBlendState = &m_ColorBlendState;
}

void
RenderPipelineState::SetDepthStencilState(
  const k3d::DepthStencilState& depthStencilState)
{
  m_DepthStencilState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  m_DepthStencilState.depthTestEnable =
    depthStencilState.DepthEnable ? VK_TRUE : VK_FALSE;
  m_DepthStencilState.depthWriteEnable = VK_TRUE;
  m_DepthStencilState.depthCompareOp =
    g_ComparisonFunc[depthStencilState.DepthFunc];
  m_DepthStencilState.depthBoundsTestEnable = VK_FALSE;
  m_DepthStencilState.back.failOp =
    g_StencilOp[depthStencilState.BackFace.StencilFailOp];
  m_DepthStencilState.back.passOp =
    g_StencilOp[depthStencilState.BackFace.StencilPassOp];
  m_DepthStencilState.back.compareOp =
    g_ComparisonFunc[depthStencilState.BackFace.StencilFunc];
  m_DepthStencilState.stencilTestEnable =
    depthStencilState.StencilEnable ? VK_TRUE : VK_FALSE;
  m_DepthStencilState.front = m_DepthStencilState.back;
  this->m_GfxCreateInfo.pDepthStencilState = &m_DepthStencilState;
}

void RenderPipelineState::SetSampler(k3d::SamplerRef)
{
}

void
RenderPipelineState::SetPrimitiveTopology(const k3d::EPrimitiveType Type)
{
  m_InputAssemblyState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  m_InputAssemblyState.topology = g_PrimitiveTopology[Type];
  this->m_GfxCreateInfo.pInputAssemblyState = &m_InputAssemblyState;
}

void
RenderPipelineState::SetRenderTargetFormat(const k3d::RenderTargetFormat&)
{
}

DynArray<VkVertexInputAttributeDescription>
RHIInputAttribs(k3d::VertexInputState ia)
{
  DynArray<VkVertexInputAttributeDescription> iad;
  for (uint32 i = 0; i < k3d::VertexInputState::kMaxVertexBindings; i++) {
    auto attrib = ia.Attribs[i];
    if (attrib.Slot == k3d::VertexInputState::kInvalidValue)
      break;
    iad.Append(
      { i, attrib.Slot, g_VertexFormatTable[attrib.Format], attrib.OffSet });
  }
  return iad;
}

DynArray<VkVertexInputBindingDescription>
RHIInputLayouts(k3d::VertexInputState const& ia)
{
  DynArray<VkVertexInputBindingDescription> ibd;
  for (uint32 i = 0; i < k3d::VertexInputState::kMaxVertexLayouts; i++) {
    auto layout = ia.Layouts[i];
    if (layout.Stride == k3d::VertexInputState::kInvalidValue)
      break;
    ibd.Append({ i, layout.Stride, g_InputRates[layout.Rate] });
  }
  return ibd;
}

void
RenderPipelineState::InitWithDesc(k3d::RenderPipelineStateDesc const& desc)
{
  // setup shaders
  if (desc.VertexShader.RawData.Length() != 0) {
    auto StageInfo = ConvertStageInfoFromShaderBundle(desc.VertexShader);
    m_ShaderStageInfos.Append(StageInfo);
  }
  if (desc.PixelShader.RawData.Length() != 0) {
    auto StageInfo = ConvertStageInfoFromShaderBundle(desc.PixelShader);
    m_ShaderStageInfos.Append(StageInfo);
  }
  if (desc.GeometryShader.RawData.Length() != 0) {
    auto StageInfo = ConvertStageInfoFromShaderBundle(desc.GeometryShader);
    m_ShaderStageInfos.Append(StageInfo);
  }
  if (desc.DomainShader.RawData.Length() != 0) {
    auto StageInfo = ConvertStageInfoFromShaderBundle(desc.DomainShader);
    m_ShaderStageInfos.Append(StageInfo);
  }
  if (desc.HullShader.RawData.Length() != 0) {
    auto StageInfo = ConvertStageInfoFromShaderBundle(desc.HullShader);
    m_ShaderStageInfos.Append(StageInfo);
  }

  // Init PrimType
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    nullptr,
    0,
    g_PrimitiveTopology[desc.PrimitiveTopology],
    VK_FALSE
  };

  // Init RasterState
  VkPipelineRasterizationStateCreateInfo rasterizationState = {
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL
  };
  rasterizationState.polygonMode = g_FillMode[desc.Rasterizer.FillMode];
  rasterizationState.cullMode = g_CullMode[desc.Rasterizer.CullMode];
  rasterizationState.frontFace = desc.Rasterizer.FrontCCW
                                   ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                   : VK_FRONT_FACE_CLOCKWISE;
  rasterizationState.depthClampEnable =
    desc.Rasterizer.DepthClipEnable ? VK_TRUE : VK_FALSE;
  rasterizationState.rasterizerDiscardEnable = VK_FALSE;
  rasterizationState.depthBiasEnable = VK_FALSE;
  rasterizationState.lineWidth = 1.0f;

  VkPipelineTessellationStateCreateInfo tessellation;

  // Init DepthStencilState
  VkPipelineDepthStencilStateCreateInfo depthStencilState = {
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL
  };
  depthStencilState.depthTestEnable =
    desc.DepthStencil.DepthEnable ? VK_TRUE : VK_FALSE;
  depthStencilState.depthWriteEnable = VK_TRUE;
  depthStencilState.depthCompareOp =
    g_ComparisonFunc[desc.DepthStencil.DepthFunc];
  depthStencilState.depthBoundsTestEnable = VK_FALSE;
  depthStencilState.back.failOp =
    g_StencilOp[desc.DepthStencil.BackFace.StencilFailOp];
  depthStencilState.back.passOp =
    g_StencilOp[desc.DepthStencil.BackFace.StencilPassOp];
  depthStencilState.back.compareOp =
    g_ComparisonFunc[desc.DepthStencil.BackFace.StencilFunc];
  depthStencilState.stencilTestEnable =
    desc.DepthStencil.StencilEnable ? VK_TRUE : VK_FALSE;
  depthStencilState.front = depthStencilState.back;

  // Init BlendState
  VkPipelineColorBlendStateCreateInfo colorBlendState = {
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL
  };
  DynArray<VkPipelineColorBlendAttachmentState> AttachmentStates;
  AttachmentStates.Resize(desc.AttachmentsBlend.Count());
  for (auto i = 0; i< desc.AttachmentsBlend.Count(); i++)
  {
    AttachmentStates[i].colorWriteMask = desc.AttachmentsBlend[i].Blend.ColorWriteMask;
    AttachmentStates[i].blendEnable = desc.AttachmentsBlend[i].Blend.Enable? VK_TRUE : VK_FALSE;
  }
  colorBlendState.attachmentCount = desc.AttachmentsBlend.Count();
  colorBlendState.pAttachments = AttachmentStates.Data();

  auto IAs = RHIInputAttribs(desc.InputState);
  auto IBs = RHIInputLayouts(desc.InputState);
  VkPipelineVertexInputStateCreateInfo vertexInputState = {
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL
  };
  vertexInputState.vertexBindingDescriptionCount = IBs.Count();
  vertexInputState.pVertexBindingDescriptions = IBs.Data();
  vertexInputState.vertexAttributeDescriptionCount = IAs.Count();
  vertexInputState.pVertexAttributeDescriptions = IAs.Data();

  VkPipelineViewportStateCreateInfo vpInfo = {
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
  };
  vpInfo.viewportCount = 1;
  vpInfo.scissorCount = 1;

  VkPipelineDynamicStateCreateInfo dynamicState = {
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
  };
  std::vector<VkDynamicState> dynamicStateEnables;
  dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);
  dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);
  dynamicStateEnables.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);
  dynamicState.pDynamicStates = dynamicStateEnables.data();
  dynamicState.dynamicStateCount = (uint32)dynamicStateEnables.size();

  VkPipelineMultisampleStateCreateInfo msInfo = {
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
  };
  msInfo.pSampleMask = NULL;
  msInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  this->m_GfxCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
  this->m_GfxCreateInfo.stageCount = m_ShaderStageInfos.Count();
  this->m_GfxCreateInfo.pStages = m_ShaderStageInfos.Data();

  this->m_GfxCreateInfo.pInputAssemblyState = &inputAssemblyState;
  this->m_GfxCreateInfo.pRasterizationState = &rasterizationState;
  this->m_GfxCreateInfo.pDepthStencilState = &depthStencilState;
  this->m_GfxCreateInfo.pColorBlendState = &colorBlendState;
  this->m_GfxCreateInfo.pVertexInputState = &vertexInputState;
  this->m_GfxCreateInfo.pDynamicState = &dynamicState;
  this->m_GfxCreateInfo.pMultisampleState = &msInfo;
  this->m_GfxCreateInfo.pViewportState = &vpInfo;

  this->m_GfxCreateInfo.renderPass = static_cast<RenderPass*>(m_WeakRenderPassRef.Get())->NativeHandle();
  this->m_GfxCreateInfo.layout = static_cast<PipelineLayout*>(m_weakPipelineLayoutRef.Get())->NativeHandle();

  // Finalize
  Rebuild();
}

void
RenderPipelineState::Destroy()
{ /*
   for (auto iter : m_ShaderStageInfos) {
     if (iter.module) {
       vkDestroyShaderModule(, iter.module, nullptr);
     }
   }*/
}

ComputePipelineState::ComputePipelineState(
  Device::Ptr pDevice,
  k3d::ComputePipelineStateDesc const& desc,
  PipelineLayout* ppl)
  : TPipelineState<k3d::IComputePipelineState>(pDevice)
  , m_ComputeCreateInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO }
  , m_PipelineLayout(ppl)
{
  auto stageInfo = ConvertStageInfoFromShaderBundle(desc.ComputeShader);
  m_ComputeCreateInfo.layout = ppl->NativeHandle();
  m_ComputeCreateInfo.stage = stageInfo;
  Rebuild();
}

void
ComputePipelineState::Rebuild()
{
  if (VK_NULL_HANDLE != m_Pipeline)
    return;
  VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
  pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  K3D_VK_VERIFY(vkCreatePipelineCache(m_Device->GetRawDevice(),
                                      &pipelineCacheCreateInfo,
                                      nullptr,
                                      &m_PipelineCache));
  K3D_VK_VERIFY(vkCreateComputePipelines(m_Device->GetRawDevice(),
                                         m_PipelineCache,
                                         1,
                                         &m_ComputeCreateInfo,
                                         nullptr,
                                         &m_Pipeline));
}

PipelineLayout::PipelineLayout(Device::Ptr pDevice,
                               k3d::PipelineLayoutDesc const& desc)
  : PipelineLayout::ThisObj(pDevice)
  , m_DescSetLayout(nullptr)
  , m_DescSet()
{
  InitWithDesc(desc);
}

PipelineLayout::~PipelineLayout()
{
  Destroy();
}

void
PipelineLayout::Destroy()
{
  if (m_NativeObj == VK_NULL_HANDLE)
    return;
  vkDestroyPipelineLayout(NativeDevice(), m_NativeObj, nullptr);
  VKLOG(Info, "PipelineLayout Destroyed . -- %0x.", m_NativeObj);
  m_NativeObj = VK_NULL_HANDLE;
}

uint64
BindingHash(Binding const& binding)
{
  return (uint64)(1 << (3 + binding.VarNumber)) | binding.VarStage;
}

bool
operator<(Binding const& lhs, Binding const& rhs)
{
  return rhs.VarStage < lhs.VarStage && rhs.VarNumber < lhs.VarNumber;
}

BindingArray
ExtractBindingsFromTable(::k3d::DynArray<Binding> const& bindings)
{
  //	merge image sampler
  std::map<uint64, Binding> bindingMap;
  for (auto const& binding : bindings) {
    uint64 hash = BindingHash(binding);
    if (bindingMap.find(hash) == bindingMap.end()) {
      bindingMap.insert({ hash, binding });
    } else // binding slot override
    {
      auto& overrideBinding = bindingMap[hash];
      if (EBindType((uint32)overrideBinding.VarType |
                    (uint32)binding.VarType) ==
          EBindType::ESamplerImageCombine) {
        overrideBinding.VarType = EBindType::ESamplerImageCombine;
      }
    }
  }

  BindingArray array;
  for (auto& p : bindingMap) {
    array.Append(RHIBinding2VkBinding(p.second));
  }
  return array;
}

void
PipelineLayout::InitWithDesc(k3d::PipelineLayoutDesc const& desc)
{
  DescriptorAllocator::Options options;
  BindingArray array = ExtractBindingsFromTable(desc.Bindings);
  auto alloc = m_Device->NewDescriptorAllocator(16, array);
  m_DescSetLayout = m_Device->NewDescriptorSetLayout(array);
  m_DescSet = k3d::DescriptorRef(DescriptorSet::CreateDescSet(
    alloc, m_DescSetLayout->GetNativeHandle(), array, m_Device));

  VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
  pPipelineLayoutCreateInfo.sType =
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pPipelineLayoutCreateInfo.pNext = NULL;
  pPipelineLayoutCreateInfo.setLayoutCount = 1;
  pPipelineLayoutCreateInfo.pSetLayouts =
    &m_DescSetLayout->m_DescriptorSetLayout;
  K3D_VK_VERIFY(vkCreatePipelineLayout(
    NativeDevice(), &pPipelineLayoutCreateInfo, nullptr, &m_NativeObj));
}

/*
enum class EBindType
{
EUndefined		= 0,
EBlock			= 0x1,
ESampler		= 0x1 << 1,
EStorageImage	= 0x1 << 2,
EStorageBuffer	= 0x1 << 3,
EConstants		= 0x000000010
};
*/
VkDescriptorType
RHIDataType2VkType(k3d::shc::EBindType const& type)
{
  switch (type) {
    case k3d::shc::EBindType::EBlock:
      return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case k3d::shc::EBindType::ESampler:
      return VK_DESCRIPTOR_TYPE_SAMPLER;
    case k3d::shc::EBindType::ESampledImage:
      return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case k3d::shc::EBindType::ESamplerImageCombine:
      return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case k3d::shc::EBindType::EStorageImage:
      return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case k3d::shc::EBindType::EStorageBuffer:
      return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

VkDescriptorSetLayoutBinding
RHIBinding2VkBinding(k3d::shc::Binding const& binding)
{
  VkDescriptorSetLayoutBinding vkbinding = {};
  vkbinding.descriptorType = RHIDataType2VkType(binding.VarType);
  vkbinding.binding = binding.VarNumber;
  vkbinding.stageFlags = g_ShaderType[binding.VarStage];
  vkbinding.descriptorCount = 1;
  return vkbinding;
}

DescriptorAllocator::DescriptorAllocator(
  Device::Ptr pDevice,
  DescriptorAllocator::Options const& option,
  uint32 maxSets,
  BindingArray const& bindings)
  : DeviceChild(pDevice)
  , m_Options(option)
  , m_Pool(VK_NULL_HANDLE)
{
  Initialize(maxSets, bindings);
}

DescriptorAllocator::~DescriptorAllocator()
{
  Destroy();
}

void
DescriptorAllocator::Initialize(uint32 maxSets, BindingArray const& bindings)
{
  std::map<VkDescriptorType, uint32_t> mappedCounts;
  for (const auto& binding : bindings) {
    VkDescriptorType descType = binding.descriptorType;
    auto it = mappedCounts.find(descType);
    if (it != mappedCounts.end()) {
      ++mappedCounts[descType];
    } else {
      mappedCounts[descType] = 1;
    }
  }

  std::vector<VkDescriptorPoolSize> typeCounts;
  for (const auto& mc : mappedCounts) {
    typeCounts.push_back({ mc.first, mc.second });
  }

  VkDescriptorPoolCreateInfo createInfo = {
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
  };
  createInfo.flags = m_Options.CreateFlags | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  createInfo.maxSets = 1;
  createInfo.poolSizeCount = typeCounts.size();
  createInfo.pPoolSizes = typeCounts.size() == 0 ? nullptr : typeCounts.data();
  K3D_VK_VERIFY(
    vkCreateDescriptorPool(GetRawDevice(), &createInfo, NULL, &m_Pool));
}

void
DescriptorAllocator::Destroy()
{
  if (VK_NULL_HANDLE == m_Pool || !GetRawDevice())
    return;
  vkDestroyDescriptorPool(GetRawDevice(), m_Pool, nullptr);
  VKLOG(Info, "DescriptorAllocator :: Destroy ... 0x%0x.", m_Pool);
  m_Pool = VK_NULL_HANDLE;
}

DescriptorSetLayout::DescriptorSetLayout(Device::Ptr pDevice,
                                         BindingArray const& bindings)
  : DeviceChild(pDevice)
  , m_DescriptorSetLayout(VK_NULL_HANDLE)
{
  Initialize(bindings);
}

DescriptorSetLayout::~DescriptorSetLayout()
{
  // Destroy();
}

void
DescriptorSetLayout::Initialize(BindingArray const& bindings)
{
  VkDescriptorSetLayoutCreateInfo createInfo = {
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
  };
  createInfo.bindingCount = bindings.Count();
  createInfo.pBindings = bindings.Count() == 0 ? nullptr : bindings.Data();
  K3D_VK_VERIFY(vkCreateDescriptorSetLayout(
    GetRawDevice(), &createInfo, nullptr, &m_DescriptorSetLayout));
}

void
DescriptorSetLayout::Destroy()
{
  if (VK_NULL_HANDLE == m_DescriptorSetLayout || !GetRawDevice())
    return;
  vkDestroyDescriptorSetLayout(GetRawDevice(), m_DescriptorSetLayout, nullptr);
  VKLOG(
    Info, "DescriptorSetLayout  destroying... -- %0x.", m_DescriptorSetLayout);
  m_DescriptorSetLayout = VK_NULL_HANDLE;
}

DescriptorSet::DescriptorSet(DescriptorAllocRef descriptorAllocator,
                             VkDescriptorSetLayout layout,
                             BindingArray const& bindings,
                             Device::Ptr pDevice)
  : DeviceChild(pDevice)
  , m_DescriptorAllocator(descriptorAllocator)
  , m_Bindings(bindings)
{
  Initialize(layout, bindings);
}

DescriptorSet*
DescriptorSet::CreateDescSet(DescriptorAllocRef descriptorPool,
                             VkDescriptorSetLayout layout,
                             BindingArray const& bindings,
                             Device::Ptr pDevice)
{
  return new DescriptorSet(descriptorPool, layout, bindings, pDevice);
}

DescriptorSet::~DescriptorSet()
{
  Destroy();
}

void
DescriptorSet::Update(uint32 bindSet, k3d::SamplerRef pRHISampler)
{
  auto pSampler = StaticPointerCast<Sampler>(pRHISampler);
  VkDescriptorImageInfo imageInfo = {
    pSampler->NativeHandle(),
    VK_NULL_HANDLE,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  };
  m_BoundDescriptorSet[bindSet].pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(
    GetRawDevice(), 1, &m_BoundDescriptorSet[bindSet], 0, NULL);
  VKLOG(Info,
        "%s , Set (0x%0x) updated with sampler(location:0x%x).",
        __K3D_FUNC__,
        m_DescriptorSet,
        pSampler->NativeHandle());
}

void
DescriptorSet::Update(uint32 bindSet, k3d::GpuResourceRef gpuResource)
{
  auto desc = gpuResource->GetDesc();
  switch (desc.Type) {
    case k3d::EGT_Buffer: {
      VkDescriptorBufferInfo bufferInfo = {
        (VkBuffer)gpuResource->GetLocation(), 0, gpuResource->GetSize()
      };
      m_BoundDescriptorSet[bindSet].pBufferInfo = &bufferInfo;
      vkUpdateDescriptorSets(
        GetRawDevice(), 1, &m_BoundDescriptorSet[bindSet], 0, NULL);
      VKLOG(Info,
            "%s , Set (0x%0x) updated with buffer(location:0x%x, size:%d).",
            __K3D_FUNC__,
            m_DescriptorSet,
            gpuResource->GetLocation(),
            gpuResource->GetSize());
      break;
    }
    case k3d::EGT_Texture1D:
    case k3d::EGT_Texture2D:
    case k3d::EGT_Texture3D:
    case k3d::EGT_Texture2DArray: // combined/seperated image sampler should be
                                  // considered
    {
      auto pTex = k3d::StaticPointerCast<Texture>(gpuResource);
      auto rSampler = k3d::StaticPointerCast<Sampler>(pTex->GetSampler());
      assert(rSampler);
      auto srv =
        k3d::StaticPointerCast<ShaderResourceView>(pTex->GetResourceView());
      VkDescriptorImageInfo imageInfo = {
        rSampler->NativeHandle(), srv->NativeImageView(), pTex->GetImageLayout()
      }; //TODO : sampler shouldn't be null
      m_BoundDescriptorSet[bindSet].pImageInfo = &imageInfo;
      vkUpdateDescriptorSets(
        GetRawDevice(), 1, &m_BoundDescriptorSet[bindSet], 0, NULL);
      VKLOG(Info,
            "%s , Set (0x%0x) updated with image(location:0x%x, size:%d).",
            __K3D_FUNC__,
            m_DescriptorSet,
            gpuResource->GetLocation(),
            gpuResource->GetSize());
      break;
    }
  }
}

uint32
DescriptorSet::GetSlotNum() const
{
  return (uint32)m_BoundDescriptorSet.size();
}

void
DescriptorSet::Initialize(VkDescriptorSetLayout layout,
                          BindingArray const& bindings)
{
  std::vector<VkDescriptorSetLayout> layouts = { layout };
  VkDescriptorSetAllocateInfo allocInfo = {
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
  };
  allocInfo.descriptorPool = m_DescriptorAllocator->m_Pool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
  allocInfo.pSetLayouts = layouts.empty() ? nullptr : layouts.data();
  K3D_VK_VERIFY(
    vkAllocateDescriptorSets(GetRawDevice(), &allocInfo, &m_DescriptorSet));
  VKLOG(Info, "%s , Set (0x%0x) created.", __K3D_FUNC__, m_DescriptorSet);

  for (auto& binding : m_Bindings) {
    VkWriteDescriptorSet entry = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    entry.dstSet = m_DescriptorSet;
    entry.descriptorCount = 1;
    entry.dstArrayElement = 0;
    entry.dstBinding = binding.binding;
    entry.descriptorType = binding.descriptorType;
    m_BoundDescriptorSet.push_back(entry);
  }
}

void
DescriptorSet::Destroy()
{
  if (VK_NULL_HANDLE == m_DescriptorSet || !GetRawDevice())
    return;

  if (m_DescriptorAllocator) {
    // const auto& options = m_DescriptorAllocator->m_Options;
    // if( options.hasFreeDescriptorSetFlag() ) {
    VkDescriptorSet descSets[1] = { m_DescriptorSet };
    vkFreeDescriptorSets(
      GetRawDevice(), m_DescriptorAllocator->m_Pool, 1, descSets);
    m_DescriptorSet = VK_NULL_HANDLE;
    //}
  }
  VKRHI_METHOD_TRACE
}

VkAttachmentDescription
ConvertAttachDesc(k3d::AttachmentDesc const& Desc)
{
  auto pTexture = StaticPointerCast<Texture>(Desc.pTexture);
  VkFormat Format = g_FormatTable[pTexture->GetDesc().TextureDesc.Format];
  VkAttachmentDescription AttachDesc = {
    0,                                // VkAttachmentDescriptionFlags    flags;
    Format,                           // VkFormat                        format;
    VK_SAMPLE_COUNT_1_BIT,            // VkSampleCountFlagBits           samples;
    g_LoadAction[Desc.LoadAction],    // VkAttachmentLoadOp              loadOp;
    g_StoreAction[Desc.StoreAction],  // VkAttachmentStoreOp             storeOp;
    g_LoadAction[Desc.LoadAction],    // VkAttachmentLoadOp              stencilLoadOp;
    g_StoreAction[Desc.StoreAction],  // VkAttachmentStoreOp             stencilStoreOp;
    VK_IMAGE_LAYOUT_UNDEFINED,        // VkImageLayout                   initialLayout;
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR         // VkImageLayout                   finalLayout;
  };
  return AttachDesc;
}

RenderPass::RenderPass(Device::Ptr pDevice, k3d::RenderPassDesc const& desc)
  : Super(pDevice)
{
  memcpy(&m_ClearVal[0].color, desc.ColorAttachments[0].ClearColor, sizeof(m_ClearVal[0].color));
#if 0
  m_ClearVal[1].depthStencil.depth = desc.pDepthAttachment->ClearDepth;
  m_ClearVal[1].depthStencil.stencil = desc.pStencilAttachment->ClearStencil;
#endif
  DynArray<VkImageView> AttachViews;
  DynArray<VkAttachmentDescription> Attachs;
  DynArray<VkAttachmentReference> ColorRefers;
  uint32 ColorIndex = 0;
  // process color attachments
  for (auto colorAttach : desc.ColorAttachments) {
    auto pTexture = StaticPointerCast<Texture>(colorAttach.pTexture);
    AttachViews.Append(pTexture->NativeView());
    Attachs.Append(ConvertAttachDesc(colorAttach));
    ColorRefers.Append({ ColorIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
    ColorIndex++;
  }

  VkSubpassDescription defaultSubpass = {
    0,                                //VkSubpassDescriptionFlags       flags;
    VK_PIPELINE_BIND_POINT_GRAPHICS,  //VkPipelineBindPoint             pipelineBindPoint;
    0,                                //uint32_t                        inputAttachmentCount;
    nullptr,                          //const VkAttachmentReference*    pInputAttachments;
    ColorRefers.Count(),              //uint32_t                        colorAttachmentCount;
    ColorRefers.Data(),               //const VkAttachmentReference*    pColorAttachments;
  //const VkAttachmentReference*    pResolveAttachments;
  //const VkAttachmentReference*    pDepthStencilAttachment;
  //uint32_t                        preserveAttachmentCount;
  //const uint32_t*                 pPreserveAttachments;
  };


  VkSubpassDependency dependency = { 0 };
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstAccessMask = 0;
  
  VkRenderPassCreateInfo renderPassCreateInfo = {
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    nullptr,
    0,
    Attachs.Count(),
    Attachs.Data(),
    1,                  // uint32_t                          subpassCount;
    &defaultSubpass,    // const VkSubpassDescription*       pSubpasses;
    1,                  // uint32_t                          dependencyCount;
    &dependency  // const VkSubpassDependency*        pDependencies
  };
  // TODO: add at least one subpass
  auto Ret = vkCreateRenderPass(
    NativeDevice(), &renderPassCreateInfo, nullptr, &m_NativeObj);
}

void RenderPass::Begin(VkCommandBuffer Cmd, SpFramebuffer pFramebuffer, VkSubpassContents contents)
{
  VkRect2D RenderArea;
  RenderArea.offset = { 0,0 };
  RenderArea.extent = { pFramebuffer->GetWidth(), pFramebuffer->GetHeight() };
  VkRenderPassBeginInfo RenderBeginInfo = {
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr,
    m_NativeObj, pFramebuffer->NativeHandle(),
    RenderArea,
    1, m_ClearVal               // uint32_t               clearValueCount;
                                // const VkClearValue*    pClearValues;
  };
  vkCmdBeginRenderPass(Cmd, &RenderBeginInfo, contents);
  VKLOG(Info, "Begin renderpass, fbo: 0x%0x.", pFramebuffer->NativeHandle());
}

void RenderPass::End(VkCommandBuffer Cmd)
{
  vkCmdEndRenderPass(Cmd);
}

void
RenderPass::Release()
{
  if (m_NativeObj) {
    vkDestroyRenderPass(NativeDevice(), m_NativeObj, nullptr);
    m_NativeObj = VK_NULL_HANDLE;
  }
}

RenderPass::~RenderPass()
{
  Release();
}

VkImageAspectFlags
DetermineAspectMask(VkFormat format)
{
  VkImageAspectFlags result = 0;
  switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT: {
      result = VK_IMAGE_ASPECT_DEPTH_BIT;
    } break;
    case VK_FORMAT_S8_UINT: {
      result = VK_IMAGE_ASPECT_STENCIL_BIT;
    } break;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT: {
      result = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    } break;
    default: {
      result = VK_IMAGE_ASPECT_COLOR_BIT;
    } break;
  }
  return result;
}

FrameBuffer::FrameBuffer(Device::Ptr pDevice, SpRenderpass pRenderPass, k3d::RenderPassDesc const& desc)
  : m_Device(pDevice)
  , m_OwningRenderPass(pRenderPass)
{
  DynArray<VkImageView> AttachViews;
  for (auto colorAttach : desc.ColorAttachments) {
    auto pTexture = StaticPointerCast<Texture>(colorAttach.pTexture);
    AttachViews.Append(pTexture->NativeView());
  }
  // create framebuffer
  VkFramebufferCreateInfo createInfo = {
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr,
  };
  // get default image size
  auto defaultImageDesc =
    StaticPointerCast<Texture>(desc.ColorAttachments[0].pTexture)
    ->GetDesc()
    .TextureDesc;
  createInfo.renderPass = m_OwningRenderPass->NativeHandle();
  createInfo.attachmentCount = AttachViews.Count();
  createInfo.pAttachments = AttachViews.Data();
  createInfo.width = defaultImageDesc.Width;
  createInfo.height = defaultImageDesc.Height;
  createInfo.layers = 1;
  createInfo.flags = 0;

  m_Width = createInfo.width;
  m_Height = createInfo.height;

  K3D_VK_VERIFY(
    vkCreateFramebuffer(m_Device->GetRawDevice(), &createInfo, nullptr, &m_FrameBuffer));
}

FrameBuffer::~FrameBuffer()
{
  if (VK_NULL_HANDLE == m_FrameBuffer) {
    return;
  }
  VKLOG(Info, "FrameBuffer destroy... -- %0x.", m_FrameBuffer);
  vkDestroyFramebuffer(m_Device->GetRawDevice(), m_FrameBuffer, nullptr);
  m_FrameBuffer = VK_NULL_HANDLE;
}
#if 0
FrameBuffer::Attachment::Attachment(VkFormat format,
                                    VkSampleCountFlagBits samples)
{
  VkImageAspectFlags aspectMask = DetermineAspectMask(Format);
  if (VK_IMAGE_ASPECT_COLOR_BIT == aspectMask) {
    FormatFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  } else {
    FormatFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
}
#endif
SwapChain::~SwapChain()
{
  Release();
}

void
SwapChain::Release()
{
  VKLOG(Info, "SwapChain Destroying..");
  if (m_SwapChain) {
    vkDestroySwapchainKHR(NativeDevice(), m_SwapChain, nullptr);
    m_SwapChain = VK_NULL_HANDLE;
  }
  if (m_Surface) {
    vkDestroySurfaceKHR(
      m_Device->m_Gpu->GetInstance()->m_Instance, m_Surface, nullptr);
    m_Surface = VK_NULL_HANDLE;
  }
  if (m_smpPresent) {
    vkDestroySemaphore(NativeDevice(), m_smpPresent, nullptr);
    m_smpPresent = VK_NULL_HANDLE;
  }
  if (m_smpRenderFinish) {
    vkDestroySemaphore(NativeDevice(), m_smpRenderFinish, nullptr);
    m_smpRenderFinish = VK_NULL_HANDLE;
  }
}

void
SwapChain::Resize(uint32 Width, uint32 Height)
{
}

k3d::TextureRef
SwapChain::GetCurrentTexture()
{
  return m_Buffers[m_CurrentBufferID];
}

void
SwapChain::Present()
{
}

void
SwapChain::QueuePresent(SpCmdQueue pQueue, k3d::SyncFenceRef pFence)
{
  VkPresentInfoKHR PresentInfo = {};
}

void
SwapChain::Init(void* pWindow, k3d::SwapChainDesc& Desc)
{
  InitSurface(pWindow);
  VkPresentModeKHR swapchainPresentMode = ChoosePresentMode();
  std::pair<VkFormat, VkColorSpaceKHR> chosenFormat = ChooseFormat(Desc.Format);
  Desc.Format = ConvertToRHIFormat(chosenFormat.first);
  m_SelectedPresentQueueFamilyIndex = ChooseQueueIndex();
  m_SwapchainExtent = { Desc.Width, Desc.Height };
  VkSurfaceCapabilitiesKHR surfProperties;
  K3D_VK_VERIFY(
    m_Device->m_Gpu->GetSurfaceCapabilitiesKHR(m_Surface, &surfProperties));
  m_SwapchainExtent = surfProperties.currentExtent;
  uint32 desiredNumBuffers = kMath::Clamp(Desc.NumBuffers,
                                          surfProperties.minImageCount,
                                          surfProperties.maxImageCount);
  m_DesiredBackBufferCount = desiredNumBuffers;
  InitSwapChain(m_DesiredBackBufferCount,
                chosenFormat,
                swapchainPresentMode,
                surfProperties.currentTransform);
  K3D_VK_VERIFY(vkGetSwapchainImagesKHR(
    NativeDevice(), m_SwapChain, &m_ReserveBackBufferCount, nullptr));
  m_ColorImages.resize(m_ReserveBackBufferCount);
  K3D_VK_VERIFY(vkGetSwapchainImagesKHR(NativeDevice(),
                                        m_SwapChain,
                                        &m_ReserveBackBufferCount,
                                        m_ColorImages.data()));
  for (auto ColorImage : m_ColorImages) {
    k3d::ResourceDesc ImageDesc;
    ImageDesc.Type = k3d::EGT_Texture2D;
    ImageDesc.ViewType = k3d::EGVT_RTV;
    ImageDesc.Flag = k3d::EGRAF_DeviceVisible;
    ImageDesc.Origin = k3d::ERO_Swapchain;
    ImageDesc.TextureDesc.Format = Desc.Format;
    ImageDesc.TextureDesc.Width = m_SwapchainExtent.width;
    ImageDesc.TextureDesc.Height = m_SwapchainExtent.height;
    ImageDesc.TextureDesc.Depth = 1;
    ImageDesc.TextureDesc.Layers = 1;
    ImageDesc.TextureDesc.MipLevels = 1;
    m_Buffers.Append(MakeShared<Texture>(ImageDesc,
      ColorImage, SharedDevice(), false));
  }
  Desc.NumBuffers = m_ReserveBackBufferCount;
  VKLOG(
    Info,
    "[SwapChain::Initialize] desired imageCount=%d, reserved imageCount = %d.",
    m_DesiredBackBufferCount,
    m_ReserveBackBufferCount);

  VkSemaphoreCreateInfo SemaphoreInfo = {
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0
  };
  vkCreateSemaphore(
    NativeDevice(), &SemaphoreInfo, nullptr, &m_smpPresent);
  vkCreateSemaphore(
    NativeDevice(), &SemaphoreInfo, nullptr, &m_smpRenderFinish);

  AcquireNextImage();
}

void
SwapChain::AcquireNextImage()
{
  VkResult Ret = vkAcquireNextImageKHR(NativeDevice(),
    m_SwapChain,
    UINT64_MAX,
    m_smpRenderFinish,
    VK_NULL_HANDLE,
    &m_CurrentBufferID);

  VKLOG(Info, "SwapChain :: AcquireNextImage, acquiring on 0x%0x, id = %d.",
    m_smpRenderFinish, m_CurrentBufferID);
  switch (Ret) {
  case VK_SUCCESS:
  case VK_SUBOPTIMAL_KHR:
    break;
  case VK_ERROR_OUT_OF_DATE_KHR:
    // OnWindowSizeChanged();
    VKLOG(Info, "Swapchain need update");
  default:
    break;
  }
  K3D_VK_VERIFY(Ret);
}

void
SwapChain::InitSurface(void* WindowHandle)
{
#if K3DPLATFORM_OS_WIN
  VkWin32SurfaceCreateInfoKHR SurfaceCreateInfo = {};
  SurfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  SurfaceCreateInfo.hinstance = GetModuleHandle(nullptr);
  SurfaceCreateInfo.hwnd = (HWND)WindowHandle;
  K3D_VK_VERIFY(
    vkCreateWin32SurfaceKHR(m_Device->m_Gpu->GetInstance()->m_Instance,
                            &SurfaceCreateInfo,
                            nullptr,
                            &m_Surface));
#elif K3DPLATFORM_OS_ANDROID
  VkAndroidSurfaceCreateInfoKHR SurfaceCreateInfo = {};
  SurfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  SurfaceCreateInfo.window = (ANativeWindow*)WindowHandle;
  K3D_VK_VERIFY(
    vkCreateAndroidSurfaceKHR(GetGpuRef()->GetInstance()->m_Instance,
                              &SurfaceCreateInfo,
                              nullptr,
                              &m_Surface));
#endif
}

VkPresentModeKHR
SwapChain::ChoosePresentMode()
{
  uint32_t presentModeCount;
  K3D_VK_VERIFY(m_Device->m_Gpu->GetSurfacePresentModesKHR(
    m_Surface, &presentModeCount, NULL));
  std::vector<VkPresentModeKHR> presentModes(presentModeCount);
  K3D_VK_VERIFY(m_Device->m_Gpu->GetSurfacePresentModesKHR(
    m_Surface, &presentModeCount, presentModes.data()));
  VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
  for (size_t i = 0; i < presentModeCount; i++) {
    if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
      swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
      break;
    }
    if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) &&
        (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)) {
      swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
  }
  return swapchainPresentMode;
}

std::pair<VkFormat, VkColorSpaceKHR>
SwapChain::ChooseFormat(k3d::EPixelFormat& Format)
{
  uint32_t formatCount;
  K3D_VK_VERIFY(
    m_Device->m_Gpu->GetSurfaceFormatsKHR(m_Surface, &formatCount, NULL));
  std::vector<VkSurfaceFormatKHR> surfFormats(formatCount);
  K3D_VK_VERIFY(m_Device->m_Gpu->GetSurfaceFormatsKHR(
    m_Surface, &formatCount, surfFormats.data()));
  VkFormat colorFormat;
  VkColorSpaceKHR colorSpace;
  if (formatCount == 1 && surfFormats[0].format == VK_FORMAT_UNDEFINED) {
    colorFormat = g_FormatTable[Format];
  } else {
    K3D_ASSERT(formatCount >= 1);
    colorFormat = surfFormats[0].format;
  }
  colorSpace = surfFormats[0].colorSpace;
  return std::make_pair(colorFormat, colorSpace);
}

int
SwapChain::ChooseQueueIndex()
{
  uint32 chosenIndex = 0;
  std::vector<VkBool32> queuePresentSupport(m_Device->GetQueueCount());

  for (uint32_t i = 0; i < m_Device->GetQueueCount(); ++i) {
    m_Device->m_Gpu->GetSurfaceSupportKHR(
      i, m_Surface, &queuePresentSupport[i]);
    if (queuePresentSupport[i]) {
      chosenIndex = i;
      break;
    }
  }

  return chosenIndex;
}

void
SwapChain::InitSwapChain(uint32 numBuffers,
                         std::pair<VkFormat, VkColorSpaceKHR> color,
                         VkPresentModeKHR mode,
                         VkSurfaceTransformFlagBitsKHR pretran)
{
  VkSwapchainCreateInfoKHR swapchainCI = {
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
  };
  swapchainCI.surface = m_Surface;
  swapchainCI.minImageCount = numBuffers;
  swapchainCI.imageFormat = color.first;
  m_ColorAttachFmt = color.first;
  swapchainCI.imageColorSpace = color.second;
  swapchainCI.imageExtent = m_SwapchainExtent;
  swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapchainCI.preTransform = pretran;
  swapchainCI.imageArrayLayers = 1;
  swapchainCI.queueFamilyIndexCount = VK_SHARING_MODE_EXCLUSIVE;
  swapchainCI.queueFamilyIndexCount = 0;
  swapchainCI.pQueueFamilyIndices = NULL;
  swapchainCI.presentMode = mode;
  swapchainCI.oldSwapchain = VK_NULL_HANDLE;
  swapchainCI.clipped = VK_TRUE;
  swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  K3D_VK_VERIFY(
    vkCreateSwapchainKHR(NativeDevice(), &swapchainCI, nullptr, &m_SwapChain));
  VKLOG(Info, "Init Swapchain with ColorFmt(%d)", m_ColorAttachFmt);
}

::k3d::DynArray<GpuRef>
Instance::EnumGpus()
{
  DynArray<GpuRef> gpus;
  DynArray<VkPhysicalDevice> gpuDevices;
  uint32_t gpuCount = 0;
  K3D_VK_VERIFY(vkEnumeratePhysicalDevices(m_Instance, &gpuCount, nullptr));
  // gpus.Resize(gpuCount);
  gpuDevices.Resize(gpuCount);
  K3D_VK_VERIFY(
    vkEnumeratePhysicalDevices(m_Instance, &gpuCount, gpuDevices.Data()));
  for (auto gpu : gpuDevices) {
    auto gpuRef = GpuRef(new Gpu(gpu, SharedFromThis()));
    gpus.Append(gpuRef);
  }
  return gpus;
}

Device::Device()
  : m_Gpu(nullptr)
  , m_Device(VK_NULL_HANDLE)
{
}

Device::~Device()
{
  Release();
}

void
Device::Release()
{
  if (VK_NULL_HANDLE == m_Device)
    return;
  vkDeviceWaitIdle(m_Device);
  VKLOG(Info, "Device Destroying .  -- %0x.", m_Device);
  if (m_CmdBufManager) {
    m_CmdBufManager->~CommandBufferManager();
  }
  vkDestroyCommandPool(m_Device, m_ImmediateCmdPool, nullptr);
  vkDestroyDevice(m_Device, nullptr);
  VKLOG(Info, "Device Destroyed .  -- %0x.", m_Device);
  m_Device = VK_NULL_HANDLE;
}

IDevice::Result
Device::Create(GpuRef const& gpu, bool withDebug)
{
  m_Gpu = gpu;
  m_Device = m_Gpu->CreateLogicDevice(withDebug);
  if (m_Device) {
#ifdef VK_NO_PROTOTYPES
    LoadVulkan(m_Gpu->m_Inst->m_Instance, m_Device);
#endif
    // Create Immediate Objects
    vkGetDeviceQueue(m_Device, 0, 0, &m_ImmediateQueue);
    if (m_ImmediateQueue)
    {
      VKLOG(Info, "Device::Create m_ImmediateQueue created.  -- %0x.", m_ImmediateQueue);
    }
    VkCommandPoolCreateInfo PoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInfo.queueFamilyIndex = m_Gpu->m_GraphicsQueueIndex;
    PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    K3D_VK_VERIFY(vkCreateCommandPool(m_Device, &PoolInfo, nullptr, &m_ImmediateCmdPool));
    if (m_ImmediateCmdPool)
    {
      VKLOG(Info, "Device::Create m_ImmediateCmdPool created.  -- %0x.", m_ImmediateCmdPool);
    }

    if (withDebug) {
      m_Gpu->m_Inst->SetupDebugging(VK_DEBUG_REPORT_ERROR_BIT_EXT |
                            VK_DEBUG_REPORT_WARNING_BIT_EXT |
                            VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
                          DebugReportCallback);
    }
    // m_ResourceManager = std::make_unique<ResourceManager>(SharedFromThis(),
    // 1024, 1024);
    return k3d::IDevice::DeviceFound;
  }
  return k3d::IDevice::DeviceNotFound;
}

uint64
Device::GetMaxAllocationCount()
{
  return m_Gpu->m_Prop.limits.maxMemoryAllocationCount;
}

SpCmdQueue
Device::InitCmdQueue(VkQueueFlags queueTypes,
                     uint32 queueFamilyIndex,
                     uint32 queueIndex)
{
  return MakeShared<CommandQueue>(
    SharedFromThis(), queueTypes, queueFamilyIndex, queueIndex);
}

VkShaderModule
Device::CreateShaderModule(k3d::ShaderBundle const& Bundle)
{
  VkShaderModule shaderModule;
  VkShaderModuleCreateInfo moduleCreateInfo;

  moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  moduleCreateInfo.pNext = NULL;

  moduleCreateInfo.codeSize = Bundle.RawData.Length();
  moduleCreateInfo.pCode = (const uint32_t*)Bundle.RawData.Data();
  moduleCreateInfo.flags = 0;
  K3D_VK_VERIFY(
    vkCreateShaderModule(m_Device, &moduleCreateInfo, NULL, &shaderModule));
  return shaderModule;
}

VkCommandBuffer
Device::AllocateImmediateCommand()
{
  VkCommandBuffer RetCmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  info.commandPool = m_ImmediateCmdPool;
  info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  info.commandBufferCount = 1;
  K3D_VK_VERIFY(vkAllocateCommandBuffers(m_Device, &info, &RetCmd));
  return RetCmd;
}

SpRenderpass Device::GetOrCreateRenderPass(const k3d::RenderPassDesc & desc)
{
  uint64 Hash = HashRenderPassDesc(desc);
  if (Hash != 0)
  {
    if (DeviceObjectCache::s_RenderPass.find(Hash) == std::end(DeviceObjectCache::s_RenderPass))
    {
      auto pRenderPass = MakeShared<RenderPass>(SharedFromThis(), desc);
      DeviceObjectCache::s_RenderPass[Hash] = pRenderPass;
    }
    else
    {
      VKLOG(Debug, "RenderPass cache Hit. 0x%0x.", DeviceObjectCache::s_RenderPass[Hash]->NativeHandle());
    }
    return SpRenderpass(DeviceObjectCache::s_RenderPass[Hash]);
  }
  else
  {
    return nullptr;
  }
}

SpFramebuffer
Device::GetOrCreateFramebuffer(const k3d::RenderPassDesc& Info)
{
  uint64 Hash = HashAttachments(Info);
  if (DeviceObjectCache::s_Framebuffer.find(Hash) == DeviceObjectCache::s_Framebuffer.end())
  {
    auto pRenderPass = CreateRenderPass(Info);
    auto pFbo = MakeShared<FrameBuffer>(SharedFromThis(), 
      StaticPointerCast<RenderPass>(pRenderPass), Info);
    DeviceObjectCache::s_Framebuffer[Hash] = pFbo;
  }
  else
  {
    VKLOG(Debug, "Framebuffer Cache Hit! (0x%0x). ", DeviceObjectCache::s_Framebuffer[Hash]->NativeHandle());
  }
  return DeviceObjectCache::s_Framebuffer[Hash];
}

bool
Device::FindMemoryType(uint32 typeBits,
                       VkFlags requirementsMask,
                       uint32* typeIndex) const
{
#ifdef max
#undef max
  *typeIndex =
    std::numeric_limits<std::remove_pointer<decltype(typeIndex)>::type>::max();
#endif
  auto memProp = m_Gpu->m_MemProp;
  for (uint32_t i = 0; i < memProp.memoryTypeCount; ++i) {
    if (typeBits & 0x00000001) {
      if (requirementsMask ==
          (memProp.memoryTypes[i].propertyFlags & requirementsMask)) {
        *typeIndex = i;
        return true;
      }
    }
    typeBits >>= 1;
  }

  return false;
}

SamplerRef
Device::NewSampler(const k3d::SamplerState& samplerDesc)
{
  return MakeShared<Sampler>(SharedFromThis(), samplerDesc);
}

k3d::PipelineLayoutKey
HashPipelineLayoutDesc(k3d::PipelineLayoutDesc const& desc)
{
  k3d::PipelineLayoutKey key;
  key.BindingKey =
    util::Hash32((const char*)desc.Bindings.Data(),
                 desc.Bindings.Count() * sizeof(k3d::shc::Binding));
  key.SetKey = util::Hash32((const char*)desc.Sets.Data(),
                            desc.Sets.Count() * sizeof(k3d::shc::Set));
  key.UniformKey =
    util::Hash32((const char*)desc.Uniforms.Data(),
                 desc.Uniforms.Count() * sizeof(k3d::shc::Uniform));
  return key;
}

k3d::PipelineLayoutRef
Device::NewPipelineLayout(k3d::PipelineLayoutDesc const& table)
{
  // Hash the table parameter here,
  // Lookup the layout by hash code
  /*k3d::PipelineLayoutKey key = HashPipelineLayoutDesc(table);
  if (m_CachedPipelineLayout.find(key) == m_CachedPipelineLayout.end())
  {
  auto plRef = k3d::PipelineLayoutRef(new PipelineLayout(this, table));
  m_CachedPipelineLayout.insert({ key, plRef });
  }*/
  return k3d::PipelineLayoutRef(new PipelineLayout(SharedFromThis(), table));
}

k3d::RenderPassRef
Device::CreateRenderPass(k3d::RenderPassDesc const& RpDesc)
{
  return GetOrCreateRenderPass(RpDesc);
}

DescriptorAllocRef
Device::NewDescriptorAllocator(uint32 maxSets, BindingArray const& bindings)
{
  uint32 key =
    util::Hash32((const char*)bindings.Data(),
                 bindings.Count() * sizeof(VkDescriptorSetLayoutBinding));
  /*if (m_CachedDescriptorPool.find(key) == m_CachedDescriptorPool.end())
  {
  DescriptorAllocator::Options options = {};
  auto descAllocRef = DescriptorAllocRef(new DescriptorAllocator(this, options,
  maxSets, bindings)); m_CachedDescriptorPool.insert({ key, descAllocRef });
  }*/
  DescriptorAllocator::Options options = {};
  return DescriptorAllocRef(
    new DescriptorAllocator(SharedFromThis(), options, maxSets, bindings));
}

DescriptorSetLayoutRef
Device::NewDescriptorSetLayout(BindingArray const& bindings)
{
  uint32 key =
    util::Hash32((const char*)bindings.Data(),
                 bindings.Count() * sizeof(VkDescriptorSetLayoutBinding));
  /*if (m_CachedDescriptorSetLayout.find(key) ==
  m_CachedDescriptorSetLayout.end())
  {
  auto descSetLayoutRef = DescriptorSetLayoutRef(new DescriptorSetLayout(this,
  bindings)); m_CachedDescriptorSetLayout.insert({ key, descSetLayoutRef });
  }*/
  return DescriptorSetLayoutRef(
    new DescriptorSetLayout(SharedFromThis(), bindings));
}

k3d::PipelineStateRef
Device::CreateRenderPipelineState(k3d::RenderPipelineStateDesc const& Desc,
                                  k3d::PipelineLayoutRef pPipelineLayout,
                                  k3d::RenderPassRef pRenderPass)
{
  return MakeShared<RenderPipelineState>(
    SharedFromThis(),
    Desc, pPipelineLayout, pRenderPass);
}

k3d::PipelineStateRef
Device::CreateComputePipelineState(k3d::ComputePipelineStateDesc const& Desc,
                                   k3d::PipelineLayoutRef pPipelineLayout)
{
  return MakeShared<ComputePipelineState>(
    SharedFromThis(),
    Desc,
    static_cast<PipelineLayout*>(pPipelineLayout.Get()));
}

SyncFenceRef
Device::CreateFence()
{
  return MakeShared<Fence>(SharedFromThis());
}

k3d::CommandQueueRef
Device::CreateCommandQueue(k3d::ECommandType const& Type)
{
  if (Type == k3d::ECMD_Graphics) {
    return MakeShared<CommandQueue>(
      SharedFromThis(), VK_QUEUE_GRAPHICS_BIT, m_Gpu->m_GraphicsQueueIndex, 0);
  } else if (Type == k3d::ECMD_Compute) {
    return MakeShared<CommandQueue>(
      SharedFromThis(), VK_QUEUE_COMPUTE_BIT, m_Gpu->m_ComputeQueueIndex, 0);
  }
  return nullptr;
}

GpuResourceRef
Device::NewGpuResource(k3d::ResourceDesc const& Desc)
{
  k3d::IGpuResource* resource = nullptr;
  switch (Desc.Type) {
    case k3d::EGT_Buffer:
      resource = new Buffer(SharedFromThis(), Desc);
      break;
    case k3d::EGT_Texture1D:
      break;
    case k3d::EGT_Texture2D:
      resource = new Texture(SharedFromThis(), Desc);
      break;
    default:
      break;
  }
  return GpuResourceRef(resource);
}

ShaderResourceViewRef
Device::NewShaderResourceView(k3d::GpuResourceRef pRes,
                              k3d::ResourceViewDesc const& desc)
{
  return MakeShared<ShaderResourceView>(SharedFromThis(), desc, pRes);
}

PtrCmdAlloc
Device::NewCommandAllocator(bool transient)
{
  return CommandAllocator::CreateAllocator(
    m_Gpu->m_GraphicsQueueIndex, transient, SharedFromThis());
}

PtrSemaphore
Device::NewSemaphore()
{
  return std::make_shared<Semaphore>(SharedFromThis());
}

void
Device::QueryTextureSubResourceLayout(k3d::TextureRef resource,
                                      k3d::TextureResourceSpec const& spec,
                                      k3d::SubResourceLayout* layout)
{
  K3D_ASSERT(resource);
  auto texture = StaticPointerCast<Texture>(resource);
  vkGetImageSubresourceLayout(m_Device,
                              (VkImage)resource->GetLocation(),
                              (const VkImageSubresource*)&spec,
                              (VkSubresourceLayout*)layout);
}

#if K3DPLATFORM_OS_WIN
#define PLATFORM_SURFACE_EXT VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#elif defined(K3DPLATFORM_OS_LINUX) && !defined(K3DPLATFORM_OS_ANDROID)
#define PLATFORM_SURFACE_EXT VK_KHR_XCB_SURFACE_EXTENSION_NAME
#elif defined(K3DPLATFORM_OS_ANDROID)
#define PLATFORM_SURFACE_EXT VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
#endif

::k3d::DynArray<VkLayerProperties> gVkLayerProps;
::k3d::DynArray<VkExtensionProperties> gVkExtProps;

Instance::Instance(const ::k3d::String& engineName,
                   const ::k3d::String& appName,
                   bool enableValidation)
  : m_EnableValidation(enableValidation)
  , m_Instance(VK_NULL_HANDLE)
  , m_DebugMsgCallback(VK_NULL_HANDLE)
{
  LoadGlobalProcs();
  EnumExtsAndLayers();
  ExtractEnabledExtsAndLayers();

  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = appName.CStr();
  appInfo.pEngineName = engineName.CStr();
  appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 1);
  appInfo.engineVersion = 1;
  appInfo.applicationVersion = 0;

  VkInstanceCreateInfo instanceCreateInfo = {};
  instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCreateInfo.pNext = NULL;
  instanceCreateInfo.pApplicationInfo = &appInfo;
  instanceCreateInfo.enabledExtensionCount = m_EnabledExtsRaw.Count();
  instanceCreateInfo.ppEnabledExtensionNames = m_EnabledExtsRaw.Data();
  instanceCreateInfo.enabledLayerCount = m_EnabledLayersRaw.Count();
  instanceCreateInfo.ppEnabledLayerNames = m_EnabledLayersRaw.Data();

  VkResult err = vkCreateInstance(&instanceCreateInfo, nullptr, &m_Instance);
  if (err == VK_ERROR_INCOMPATIBLE_DRIVER) {
    VKLOG(Error,
          "Cannot find a compatible Vulkan installable client driver: "
          "vkCreateInstance Failure");
  } else if (err == VK_ERROR_EXTENSION_NOT_PRESENT) {
    VKLOG(
      Error,
      "Cannot find a specified extension library: vkCreateInstance Failure");
  } else {
    LoadInstanceProcs();
    EnumGpus();
  }
}

Instance::~Instance()
{
  Release();
}

void
Instance::Release()
{
  VKLOG(Info,
        "Instance Destroying...  -- %0x. (tid:%d)",
        m_Instance,
        Os::Thread::GetId());
  if (m_Instance) {
    FreeDebugCallback();
    vkDestroyInstance(m_Instance, nullptr);
    VKLOG(Info, "Instance Destroyed .  -- %0x.", m_Instance);
    m_Instance = VK_NULL_HANDLE;
  }
}

void
Instance::EnumDevices(DynArray<k3d::DeviceRef>& Devices)
{
  auto GpuRefs = EnumGpus();
  for (auto& gpu : GpuRefs) {
    auto pDevice = new Device;
    pDevice->Create(gpu->SharedFromThis(), WithValidation());
    Devices.Append(k3d::DeviceRef(pDevice));
  }
}

k3d::SwapChainRef
Instance::CreateSwapchain(k3d::CommandQueueRef pCommandQueue,
                          void* nPtr,
                          k3d::SwapChainDesc& Desc)
{
  SpCmdQueue pQueue = StaticPointerCast<CommandQueue>(pCommandQueue);
  return MakeShared<SwapChain>(pQueue->SharedDevice(), nPtr, Desc);
}

#ifdef VK_NO_PROTOTYPES
#define __VK_GLOBAL_PROC_GET__(name, functor)                                  \
  gp##name = reinterpret_cast<PFN_vk##name>(functor("vk" K3D_STRINGIFY(name)))
#endif
void
Instance::LoadGlobalProcs()
{
#ifdef VK_NO_PROTOTYPES
#ifdef K3DPLATFORM_OS_WIN
  static const char* LIBVULKAN = "vulkan-1.dll";
#elif defined(K3DPLATFORM_OS_MAC)
  static const char* LIBVULKAN = "libvulkan.dylib";
#else
  static const char* LIBVULKAN = "libvulkan.so";
#endif

  m_VulkanLib = MakeShared<dynlib::Lib>(LIBVULKAN);
  // load global functions
  __VK_GLOBAL_PROC_GET__(CreateInstance, m_VulkanLib->ResolveEntry);
  __VK_GLOBAL_PROC_GET__(DestroyInstance, m_VulkanLib->ResolveEntry);
  __VK_GLOBAL_PROC_GET__(EnumerateInstanceExtensionProperties,
                         m_VulkanLib->ResolveEntry);
  __VK_GLOBAL_PROC_GET__(EnumerateInstanceLayerProperties,
                         m_VulkanLib->ResolveEntry);
  __VK_GLOBAL_PROC_GET__(GetInstanceProcAddr, m_VulkanLib->ResolveEntry);
#endif
}

void
Instance::EnumExtsAndLayers()
{
  uint32 layerCount = 0;
  K3D_VK_VERIFY(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
  if (layerCount > 0) {
    gVkLayerProps.Resize(layerCount);
    K3D_VK_VERIFY(
      vkEnumerateInstanceLayerProperties(&layerCount, gVkLayerProps.Data()));
  }
  uint32 extCount = 0;
  K3D_VK_VERIFY(
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr));
  if (extCount > 0) {
    gVkExtProps.Resize(extCount);
    K3D_VK_VERIFY(vkEnumerateInstanceExtensionProperties(
      nullptr, &extCount, gVkExtProps.Data()));
  }
  VKLOG(Info,
        ">> Instance::EnumLayersAndExts <<\n"
        "=================================>> layerCount = %d.\n"
        "=================================>> extensionCount = %d.\n",
        layerCount,
        extCount);
}

void
Instance::ExtractEnabledExtsAndLayers()
{
  VkBool32 surfaceExtFound = 0;
  VkBool32 platformSurfaceExtFound = 0;

  for (uint32_t i = 0; i < gVkExtProps.Count(); i++) {
    if (!strcmp(VK_KHR_SURFACE_EXTENSION_NAME, gVkExtProps[i].extensionName)) {
      surfaceExtFound = 1;
      m_EnabledExts.Append(VK_KHR_SURFACE_EXTENSION_NAME);
      m_EnabledExtsRaw.Append(VK_KHR_SURFACE_EXTENSION_NAME);
    }
    if (!strcmp(PLATFORM_SURFACE_EXT, gVkExtProps[i].extensionName)) {
      platformSurfaceExtFound = 1;
      m_EnabledExts.Append(PLATFORM_SURFACE_EXT);
      m_EnabledExtsRaw.Append(PLATFORM_SURFACE_EXT);
    }
    if (m_EnableValidation) {
      m_EnabledExts.Append(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
      m_EnabledExtsRaw.Append(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }
    VKLOG(Info, "available extension : %s .", gVkExtProps[i].extensionName);
  }
  if (!surfaceExtFound) {
    VKLOG(Error,
          "vkEnumerateInstanceExtensionProperties failed to find "
          "the " VK_KHR_SURFACE_EXTENSION_NAME " extension.");
  }
  if (!platformSurfaceExtFound) {
    VKLOG(Error,
          "vkEnumerateInstanceExtensionProperties failed to find "
          "the " PLATFORM_SURFACE_EXT " extension.");
  }

  if (m_EnableValidation && !gVkLayerProps.empty()) {
    for (auto prop : gVkLayerProps) {
      if (strcmp(prop.layerName, g_ValidationLayerNames[0]) ==
          0 /*|| strcmp(prop.layerName, g_ValidationLayerNames[1])==0*/) {
        m_EnabledLayers.Append(prop.layerName);
        m_EnabledLayersRaw.Append(g_ValidationLayerNames[0]);
        VKLOG(Info, "enable validation layer [%s].", prop.layerName);
        break;
      }
    }
  }
}

void
Instance::LoadInstanceProcs()
{
  CreateDebugReport = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugReportCallbackEXT");
#ifdef VK_NO_PROTOTYPES
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceSurfaceSupportKHR);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceSurfaceFormatsKHR);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceSurfacePresentModesKHR);

  GET_INSTANCE_PROC_ADDR(m_Instance, EnumeratePhysicalDevices);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceFeatures);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceProperties);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceMemoryProperties);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceFormatProperties);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetPhysicalDeviceQueueFamilyProperties);
  GET_INSTANCE_PROC_ADDR(m_Instance, CreateDevice);
  GET_INSTANCE_PROC_ADDR(m_Instance, GetDeviceProcAddr);

#if K3DPLATFORM_OS_WIN
  GET_INSTANCE_PROC_ADDR(m_Instance, CreateWin32SurfaceKHR);
#elif K3DPLATFORM_OS_ANDROID
  GET_INSTANCE_PROC_ADDR(m_Instance, CreateAndroidSurfaceKHR);
#endif
  GET_INSTANCE_PROC_ADDR(m_Instance, DestroySurfaceKHR);
#endif
}

PFN_vkDebugReportMessageEXT dbgBreakCallback;

static VkDebugReportCallbackEXT msgCallback = NULL;

const char*
DebugLevelString(VkDebugReportFlagsEXT lev)
{
  switch (lev) {
#define STR(r)                                                                 \
  case VK_DEBUG_REPORT_##r##_BIT_EXT:                                          \
    return #r
    STR(INFORMATION);
    STR(WARNING);
    STR(PERFORMANCE_WARNING);
    STR(ERROR);
    STR(DEBUG);
#undef STR
    default:
      return "UNKNOWN_ERROR";
  }
}

VKAPI_ATTR VkBool32 VKAPI_CALL
DebugReportCallback(VkDebugReportFlagsEXT flags,
                    VkDebugReportObjectTypeEXT objectType,
                    uint64_t object,
                    size_t location,
                    int32_t messageCode,
                    const char* pLayerPrefix,
                    const char* pMessage,
                    void* pUserData)
{
  static char msg[4096] = { 0 };
  switch (flags) {
    case VK_DEBUG_REPORT_WARNING_BIT_EXT:
    case VK_DEBUG_REPORT_ERROR_BIT_EXT:
      VKLOG(Error, "[%s]\t%s\t[location] %d", pLayerPrefix, pMessage, location);
      break;
    case VK_DEBUG_REPORT_INFORMATION_BIT_EXT:
      VKLOG(Info, "[%s]\t%s\t[location] %d", pLayerPrefix, pMessage, location);
      break;
    case VK_DEBUG_REPORT_DEBUG_BIT_EXT:
      VKLOG(Debug, "[%s]\t%s\t[location] %d", pLayerPrefix, pMessage, location);
      break;
    default:
      VKLOG(
        Default, "[%s]\t%s\t[location] %d", pLayerPrefix, pMessage, location);
      break;
  }
  return VK_TRUE;
}

#define __VK_GLOBAL_PROC_GET__(name, functor)                                  \
  fp##name = reinterpret_cast<PFN_vk##name>(functor("vk" K3D_STRINGIFY(name)))

void
Instance::SetupDebugging(VkDebugReportFlagsEXT flags,
                         PFN_vkDebugReportCallbackEXT callBack)
{
  if (!m_Instance) {
    VKLOG(Error, "SetupDebugging Failed. (m_Instance == null)");
    return;
  }
#ifdef VK_NO_PROTOTYPES
  __VK_GLOBAL_PROC_GET__(CreateDebugReportCallbackEXT,
                         m_VulkanLib->ResolveEntry);
  if (!vkCreateDebugReportCallbackEXT)
    GET_INSTANCE_PROC_ADDR(m_Instance, CreateDebugReportCallbackEXT);
  __VK_GLOBAL_PROC_GET__(DestroyDebugReportCallbackEXT,
                         m_VulkanLib->ResolveEntry);
  if (!fpDestroyDebugReportCallbackEXT)
    GET_INSTANCE_PROC_ADDR(m_Instance, DestroyDebugReportCallbackEXT);
#endif
  VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
  dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
  dbgCreateInfo.pNext = NULL;
  dbgCreateInfo.pfnCallback = callBack;
  dbgCreateInfo.pUserData = NULL;
  dbgCreateInfo.flags = flags;
  K3D_VK_VERIFY(CreateDebugReport(m_Instance, &dbgCreateInfo, NULL, &m_DebugMsgCallback));
}

void
Instance::FreeDebugCallback()
{
  if (m_Instance && m_DebugMsgCallback) {
    //vkDestroyDebugReportCallbackEXT(m_Instance, m_DebugMsgCallback, nullptr);
    m_DebugMsgCallback = VK_NULL_HANDLE;
  }
}


CommandBufferManager::CommandBufferManager(VkDevice gpu, VkCommandBufferLevel bufferLevel,
  unsigned graphicsQueueIndex)
  : m_Device(gpu)
  , m_CommandBufferLevel(bufferLevel)
  , m_Count(0)
{
  // RESET_COMMAND_BUFFER_BIT allows command buffers to be reset individually.
  VkCommandPoolCreateInfo info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
  info.queueFamilyIndex = graphicsQueueIndex;
  info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  K3D_VK_VERIFY(vkCreateCommandPool(m_Device, &info, nullptr, &m_Pool));
}

CommandBufferManager::~CommandBufferManager()
{
  Destroy();
}

void CommandBufferManager::Destroy()
{
  if (!m_Pool)
    return;
  vkDeviceWaitIdle(m_Device);
  VKLOG(Info, "CommandBufferManager destroy. -- 0x%0x.", m_Pool);
  vkFreeCommandBuffers(m_Device, m_Pool, m_Buffers.Count(), m_Buffers.Data());
  vkDestroyCommandPool(m_Device, m_Pool, nullptr);
  m_Pool = VK_NULL_HANDLE;
}

void CommandBufferManager::BeginFrame()
{
  m_Count = 0;
}

VkCommandBuffer CommandBufferManager::RequestCommandBuffer()
{
  // Either we recycle a previously allocated command buffer, or create a new one.
  VkCommandBuffer ret = VK_NULL_HANDLE;
  if (m_Count < m_Buffers.Count())
  {
    ret = m_Buffers[m_Count++];
    K3D_VK_VERIFY(vkResetCommandBuffer(ret, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT));
  }
  else
  {
    VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    info.commandPool = m_Pool;
    info.level = m_CommandBufferLevel;
    info.commandBufferCount = 1;
    K3D_VK_VERIFY(vkAllocateCommandBuffers(m_Device, &info, &ret));
    m_Buffers.Append(ret);

    m_Count++;
  }

  return ret;
}

Gpu::Gpu(VkPhysicalDevice const& gpu, InstanceRef const& pInst)
  : m_Inst(pInst)
  , m_LogicalDevice(VK_NULL_HANDLE)
  , m_PhysicalGpu(gpu)
{
  vkGetPhysicalDeviceProperties(m_PhysicalGpu, &m_Prop);
  vkGetPhysicalDeviceMemoryProperties(m_PhysicalGpu, &m_MemProp);
  vkGetPhysicalDeviceFeatures(m_PhysicalGpu, &m_Features);
  VKLOG(Info, "Gpu: %s", m_Prop.deviceName);
  QuerySupportQueues();
}

void Gpu::QuerySupportQueues()
{
  uint32 queueCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalGpu, &queueCount, NULL);
  if (queueCount < 1)
    return;
  m_QueueProps.Resize(queueCount);
  vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalGpu, &queueCount, m_QueueProps.Data());
  uint32 qId = 0;
  for (qId = 0; qId < queueCount; qId++)
  {
    if (m_QueueProps[qId].queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      m_GraphicsQueueIndex = qId;
      VKLOG(Info, "Device::graphicsQueueIndex(%d) queueFlags(%d).", m_GraphicsQueueIndex, m_QueueProps[qId].queueFlags);
      break;
    }
  }
  for (qId = 0; qId < queueCount; qId++)
  {
    if (m_QueueProps[qId].queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      m_ComputeQueueIndex = qId;
      VKLOG(Info, "Device::ComputeQueueIndex(%d).", m_ComputeQueueIndex);
      break;
    }
  }
  for (qId = 0; qId < queueCount; qId++)
  {
    if (m_QueueProps[qId].queueFlags & VK_QUEUE_TRANSFER_BIT)
    {
      m_CopyQueueIndex = qId;
      VKLOG(Info, "Device::CopyQueueIndex(%d).", m_CopyQueueIndex);
      break;
    }
  }
}

#define __VK_GET_DEVICE_PROC__(name, getDeviceProc, device) \
if(!vk##name) \
{\
vk##name = (PFN_vk##name)getDeviceProc(device, "vk" K3D_STRINGIFY(name)); \
if (!vk##name) \
{\
VKLOG(Fatal, "LoadDeviceProcs::" K3D_STRINGIFY(name) " not exist!" );\
exit(-1);\
}\
}

void Gpu::LoadDeviceProcs()
{
#ifdef VK_NO_PROTOTYPES
  if (!fpDestroyDevice)
  {
    fpDestroyDevice = (PFN_vkDestroyDevice)m_Inst->fpGetDeviceProcAddr(m_LogicalDevice, "vkDestroyDevice");
  }
  if (!fpFreeCommandBuffers)
  {
    fpFreeCommandBuffers = (PFN_vkFreeCommandBuffers)m_Inst->fpGetDeviceProcAddr(m_LogicalDevice, "vkFreeCommandBuffers");
  }
  if (!fpCreateCommandPool)
  {
    fpCreateCommandPool = (PFN_vkCreateCommandPool)m_Inst->fpGetDeviceProcAddr(m_LogicalDevice, "vkCreateCommandPool");
  }
  if (!fpAllocateCommandBuffers)
  {
    fpAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)m_Inst->fpGetDeviceProcAddr(m_LogicalDevice, "vkAllocateCommandBuffers");
  }
  __VK_GET_DEVICE_PROC__(DestroyDevice, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetDeviceQueue, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(QueueSubmit, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(QueueWaitIdle, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(QueuePresentKHR, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DeviceWaitIdle, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(AllocateMemory, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(FreeMemory, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(MapMemory, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(UnmapMemory, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(FlushMappedMemoryRanges, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(InvalidateMappedMemoryRanges, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetDeviceMemoryCommitment, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(BindBufferMemory, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(BindImageMemory, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetBufferMemoryRequirements, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetImageMemoryRequirements, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetImageSparseMemoryRequirements, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(QueueBindSparse, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateFence, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyFence, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(ResetFences, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetFenceStatus, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(WaitForFences, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateSemaphore, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroySemaphore, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateEvent, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyEvent, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetEventStatus, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(SetEvent, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(ResetEvent, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateQueryPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyQueryPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetQueryPoolResults, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateBufferView, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyBufferView, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateImage, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyImage, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetImageSubresourceLayout, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateImageView, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyImageView, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateShaderModule, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyShaderModule, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreatePipelineCache, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyPipelineCache, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetPipelineCacheData, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(MergePipelineCaches, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateGraphicsPipelines, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateComputePipelines, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyPipeline, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreatePipelineLayout, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyPipelineLayout, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateSampler, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroySampler, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateDescriptorSetLayout, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyDescriptorSetLayout, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateDescriptorPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyDescriptorPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(ResetDescriptorPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(AllocateDescriptorSets, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(FreeDescriptorSets, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(UpdateDescriptorSets, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateFramebuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyFramebuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateRenderPass, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyRenderPass, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetRenderAreaGranularity, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateCommandPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroyCommandPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(ResetCommandPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(AllocateCommandBuffers, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(FreeCommandBuffers, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(BeginCommandBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(EndCommandBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(ResetCommandBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdBindPipeline, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetViewport, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetScissor, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetLineWidth, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetDepthBias, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetBlendConstants, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetDepthBounds, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetStencilCompareMask, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetStencilWriteMask, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetStencilReference, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdBindDescriptorSets, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdBindIndexBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdBindVertexBuffers, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdDraw, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdDrawIndexed, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdDrawIndirect, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdDrawIndexedIndirect, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdDispatch, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdDispatchIndirect, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdCopyBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdCopyImage, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdBlitImage, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdCopyBufferToImage, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdCopyImageToBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdUpdateBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdFillBuffer, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdClearColorImage, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdClearDepthStencilImage, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdClearAttachments, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdResolveImage, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdSetEvent, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdResetEvent, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdWaitEvents, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdPipelineBarrier, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdBeginQuery, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdEndQuery, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdResetQueryPool, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdWriteTimestamp, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdCopyQueryPoolResults, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdPushConstants, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdBeginRenderPass, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdNextSubpass, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdEndRenderPass, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CmdExecuteCommands, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(AcquireNextImageKHR, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(CreateSwapchainKHR, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(DestroySwapchainKHR, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
  __VK_GET_DEVICE_PROC__(GetSwapchainImagesKHR, m_Inst->fpGetDeviceProcAddr, m_LogicalDevice);
#endif
}

Gpu::~Gpu()
{
  VKLOG(Info, "Gpu Destroyed....");
  if (m_PhysicalGpu)
  {
    m_PhysicalGpu = VK_NULL_HANDLE;
  }
}

VkDevice Gpu::CreateLogicDevice(bool enableValidation)
{
  if (!m_LogicalDevice)
  {
    std::array<float, 1> queuePriorities = { 0.0f };
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_GraphicsQueueIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = queuePriorities.data();

    std::vector<const char*> enabledExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = NULL;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.pEnabledFeatures = &m_Features;

    if (enableValidation)
    {
      deviceCreateInfo.enabledLayerCount = 1;
      deviceCreateInfo.ppEnabledLayerNames = g_ValidationLayerNames;
    }

    if (enabledExtensions.size() > 0)
    {
      deviceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
      deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
    }
    K3D_VK_VERIFY(vkCreateDevice(m_PhysicalGpu, &deviceCreateInfo, nullptr, &m_LogicalDevice));

    LoadDeviceProcs();
  }
  return m_LogicalDevice;
}

VkBool32 Gpu::GetSupportedDepthFormat(VkFormat * depthFormat)
{
  // Since all depth formats may be optional, we need to find a suitable depth format to use
  // Start with the highest precision packed format
  std::vector<VkFormat> depthFormats = {
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM
  };

  for (auto& format : depthFormats)
  {
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(m_PhysicalGpu, format, &formatProps);
    // Format must support depth stencil attachment for optimal tiling
    if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
      *depthFormat = format;
      return true;
    }
  }

  return false;
}

VkResult Gpu::GetSurfaceSupportKHR(uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 * pSupported)
{
  return vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalGpu, queueFamilyIndex, surface, pSupported);
}

VkResult Gpu::GetSurfaceCapabilitiesKHR(VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR * pSurfaceCapabilities)
{
  return vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalGpu, surface, pSurfaceCapabilities);
}

VkResult Gpu::GetSurfaceFormatsKHR(VkSurfaceKHR surface, uint32_t * pSurfaceFormatCount, VkSurfaceFormatKHR * pSurfaceFormats)
{
  return vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalGpu, surface, pSurfaceFormatCount, pSurfaceFormats);
}

VkResult Gpu::GetSurfacePresentModesKHR(VkSurfaceKHR surface, uint32_t * pPresentModeCount, VkPresentModeKHR * pPresentModes)
{
  return vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalGpu, surface, pPresentModeCount, pPresentModes);
}

void Gpu::DestroyDevice()
{
  vkDestroyDevice(m_LogicalDevice, nullptr);
}

void Gpu::FreeCommandBuffers(VkCommandPool pool, uint32 count, VkCommandBuffer * cmd)
{
  vkFreeCommandBuffers(m_LogicalDevice, pool, count, cmd);
}

VkResult Gpu::CreateCommdPool(const VkCommandPoolCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkCommandPool * pCommandPool)
{
  return vkCreateCommandPool(m_LogicalDevice, pCreateInfo, pAllocator, pCommandPool);
}

VkResult Gpu::AllocateCommandBuffers(const VkCommandBufferAllocateInfo * pAllocateInfo, VkCommandBuffer * pCommandBuffers)
{
  return vkAllocateCommandBuffers(m_LogicalDevice, pAllocateInfo, pCommandBuffers);
}

void
RHIRoot::EnumLayersAndExts()
{
  // Enum Layers
  uint32 layerCount = 0;
  K3D_VK_VERIFY(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
  if (layerCount > 0) {
    gVkLayerProps.Resize(layerCount);
    K3D_VK_VERIFY(
      vkEnumerateInstanceLayerProperties(&layerCount, gVkLayerProps.Data()));
  }

  // Enum Extensions
  uint32 extCount = 0;
  K3D_VK_VERIFY(
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr));
  if (extCount > 0) {
    gVkExtProps.Resize(extCount);
    K3D_VK_VERIFY(vkEnumerateInstanceExtensionProperties(
      nullptr, &extCount, gVkExtProps.Data()));
  }
  VKLOG(Info,
        ">> RHIRoot::EnumLayersAndExts <<\n\n"
        "=================================>> layerCount = %d.\n"
        "=================================>> extensionCount = %d.\n",
        layerCount,
        extCount);
}

class RHIImpl
  : public ::k3d::IVkRHI
  , public ::k3d::EnableSharedFromThis<RHIImpl>
{
public:
  RHIImpl() {}

  ~RHIImpl() override
  {
    VKLOG(Info, "Destroying..");
    Shutdown();
  }

  void Initialize(const char* appName, bool debug) override
  {
    if (!m_Instance) {
      m_Instance = k3d::MakeShared<k3d::vk::Instance>(appName, appName, debug);
    }
  }

  k3d::FactoryRef GetFactory() { return m_Instance; }

  void Start() override
  {
    if (!m_Instance) {
      VKLOG(Fatal,
            "RHI Module uninitialized!! You should call Initialize first!");
      return;
    }
  }

  void Shutdown() override { KLOG(Info, VKRHI, "Shuting down..."); }

  const char* Name() { return "RHI_Vulkan"; }

private:
  k3d::vk::InstanceRef m_Instance;
};

K3D_VK_END

// RHI Exposed Interface
MODULE_IMPLEMENT(RHI_Vulkan, k3d::vk::RHIImpl)