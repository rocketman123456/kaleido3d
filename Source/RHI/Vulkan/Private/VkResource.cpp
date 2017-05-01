#include "VkCommon.h"
#include "VkEnums.h"
#include "VkRHI.h"
#include <algorithm>

K3D_VK_BEGIN

VkDeviceSize
CalcAlignedOffset(VkDeviceSize offset, VkDeviceSize align)
{
  VkDeviceSize n = offset / align;
  VkDeviceSize r = offset % align;
  VkDeviceSize result = (n + (r > 0 ? 1 : 0)) * align;
  return result;
}
// Buffer functors
decltype(vkCreateBufferView)* ResTrait<VkBuffer>::CreateView =
  &vkCreateBufferView;
decltype(vkDestroyBufferView)* ResTrait<VkBuffer>::DestroyView =
  &vkDestroyBufferView;
decltype(vkCreateBuffer)* ResTrait<VkBuffer>::Create = &vkCreateBuffer;
decltype(vkDestroyBuffer)* ResTrait<VkBuffer>::Destroy = &vkDestroyBuffer;
decltype(vkGetBufferMemoryRequirements)* ResTrait<VkBuffer>::GetMemoryInfo =
  &vkGetBufferMemoryRequirements;
decltype(vkBindBufferMemory)* ResTrait<VkBuffer>::BindMemory =
  &vkBindBufferMemory;

decltype(vkCreateImageView)* ResTrait<VkImage>::CreateView = &vkCreateImageView;
decltype(vkDestroyImageView)* ResTrait<VkImage>::DestroyView =
  &vkDestroyImageView;
decltype(vkCreateImage)* ResTrait<VkImage>::Create = &vkCreateImage;
decltype(vkDestroyImage)* ResTrait<VkImage>::Destroy = &vkDestroyImage;
decltype(vkGetImageMemoryRequirements)* ResTrait<VkImage>::GetMemoryInfo =
  &vkGetImageMemoryRequirements;
decltype(vkBindImageMemory)* ResTrait<VkImage>::BindMemory = &vkBindImageMemory;

Buffer::Buffer(Device::Ptr pDevice, k3d::ResourceDesc const& desc)
  : TResource<VkBuffer, k3d::IGpuResource>(pDevice, desc)
{
  m_ResUsageFlags = g_ResourceViewFlag[desc.ViewType];

  if (desc.CreationFlag & k3d::EGRCF_TransferSrc) {
    m_ResUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }

  if (desc.CreationFlag & k3d::EGRCF_TransferDst) {
    m_ResUsageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  if (desc.Flag & k3d::EGRAF_HostVisible) {
    m_MemoryBits |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }
  if (desc.Flag & k3d::EGRAF_DeviceVisible) {
    m_MemoryBits |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }
  if (desc.Flag & k3d::EGRAF_HostCoherent) {
    m_MemoryBits |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }
  Create(desc.Size);
}

Buffer::~Buffer()
{
  VKLOG(Info, "Buffer Destroying..");
}

void
Buffer::Create(size_t size)
{
  VkBufferCreateInfo createInfo;
  createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  createInfo.pNext = nullptr;
  createInfo.size = size;
  createInfo.usage = m_ResUsageFlags;
  createInfo.flags = 0;
  createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.queueFamilyIndexCount = 0;
  createInfo.pQueueFamilyIndices = nullptr;

  TResource<VkBuffer, k3d::IGpuResource>::Allocate(createInfo);

  m_ResDescInfo.buffer = m_NativeObj;
  m_ResDescInfo.offset = 0;
  m_ResDescInfo.range = m_MemAllocInfo.allocationSize;

  K3D_VK_VERIFY(
    vkBindBufferMemory(NativeDevice(), m_NativeObj, m_DeviceMem, 0));
}

Texture::Texture(Device::Ptr pDevice, k3d::ResourceDesc const& desc)
  : Texture::ThisResourceType(pDevice, desc)
{
  // TODO
  if (desc.CreationFlag & k3d::EGRCF_TransferDst) {
    m_ResUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  if (desc.Flag & k3d::EGRAF_HostVisible) {
    m_MemoryBits |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }
  if (desc.Flag & k3d::EGRAF_DeviceVisible) {
    m_MemoryBits |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }
  if (desc.Flag & k3d::EGRAF_HostCoherent) {
    m_MemoryBits |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }
  switch (desc.ViewType) {
    case k3d::EGVT_SRV:
      m_SubResRange = { VK_IMAGE_ASPECT_COLOR_BIT,
                        0,
                        desc.TextureDesc.MipLevels,
                        0,
                        desc.TextureDesc.Layers };
      CreateSampledTexture(desc.TextureDesc);
      break;
    case k3d::EGVT_RTV:
      m_SubResRange = { VK_IMAGE_ASPECT_COLOR_BIT,
                        0,
                        desc.TextureDesc.MipLevels,
                        0,
                        desc.TextureDesc.Layers };
      CreateRenderTexture(desc.TextureDesc);
      break;
    case k3d::EGVT_DSV:
      m_SubResRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                        0,
                        desc.TextureDesc.MipLevels,
                        0,
                        desc.TextureDesc.Layers };
      CreateRenderTexture(desc.TextureDesc);
      break;
  }
}

Texture::Texture(k3d::ResourceDesc const& Desc,
                 VkImage image,
                 // VkImageView imageView,
                 Device::Ptr pDevice,
                 bool selfOwnShip)
  : Texture::ThisResourceType(pDevice, selfOwnShip)
{
  m_ResDesc = Desc;
  m_NativeObj = image;
  //  m_ResView = imageView;
  m_ImageViewInfo = {};
  m_SubResRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  K3D_ASSERT(m_ResDesc.Origin == k3d::ERO_Swapchain);
  CreateViewForSwapchainImage();
#if 0
  auto Cmd = m_Device->AllocateImmediateCommand();

  VkCommandBufferBeginInfo BeginInfo = {};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  BeginInfo.pNext = nullptr;
  BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  BeginInfo.pInheritanceInfo = nullptr;
  vkBeginCommandBuffer(Cmd, &BeginInfo);
  VkImageLayout SrcLayout = g_ResourceState[GetState()];
  VkImageLayout DestLayout = g_ResourceState[k3d::ERS_RenderTarget];
  VkImageMemoryBarrier ImageBarrier = {
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    nullptr,
    0,
    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    SrcLayout,
    DestLayout,
    VK_QUEUE_FAMILY_IGNORED,
    VK_QUEUE_FAMILY_IGNORED,
    m_NativeObj,
    m_SubResRange
  };
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0, // DependencyFlag
    0,
    nullptr,
    0,
    nullptr,
    1,
    &ImageBarrier);
  vkEndCommandBuffer(Cmd);

  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = 0;
  VkFence fence;
  K3D_VK_VERIFY(vkCreateFence(NativeDevice(), &fenceCreateInfo, nullptr, &fence));
  VkSubmitInfo SubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
  SubmitInfo.commandBufferCount = 1;
  SubmitInfo.pCommandBuffers = &Cmd;
  K3D_VK_VERIFY(vkQueueSubmit(m_Device->GetImmQueue(), 1, &SubmitInfo, fence));
  vkQueueWaitIdle(m_Device->GetImmQueue());
  K3D_VK_VERIFY(vkWaitForFences(NativeDevice(), 1, &fence, VK_TRUE, 1000));
  vkDestroyFence(NativeDevice(), fence, nullptr);
  vkFreeCommandBuffers(NativeDevice(), ImmCmdPool(), 1, &Cmd);
  VKLOG(Info, "Transition finished.");
  m_UsageState = k3d::ERS_RenderTarget;
#endif
}

void
Texture::BindSampler(k3d::SamplerRef sampler)
{
  m_ImageSampler = k3d::StaticPointerCast<Sampler>(sampler);
}

SamplerCRef
Texture::GetSampler() const
{
  return m_ImageSampler;
}

void
Texture::CreateResourceView()
{
  // TODO
  m_ImageViewInfo = { 
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    nullptr,
    0,
    m_NativeObj,
    ConvertFromTextureType(m_ResDesc.Type),
    g_FormatTable[m_ResDesc.TextureDesc.Format],
    { VK_COMPONENT_SWIZZLE_R,
    VK_COMPONENT_SWIZZLE_G,
    VK_COMPONENT_SWIZZLE_B,
    VK_COMPONENT_SWIZZLE_A },
    m_SubResRange
  };
  K3D_VK_VERIFY(
    vkCreateImageView(NativeDevice(), &m_ImageViewInfo, nullptr, &m_ResView));
}

void
Texture::CreateViewForSwapchainImage()
{
  m_ImageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                      nullptr,
                      0,
                      m_NativeObj,
                      VK_IMAGE_VIEW_TYPE_2D,
                      g_FormatTable[m_ResDesc.TextureDesc.Format],
                      { VK_COMPONENT_SWIZZLE_R,
                        VK_COMPONENT_SWIZZLE_G,
                        VK_COMPONENT_SWIZZLE_B,
                        VK_COMPONENT_SWIZZLE_A },
                      m_SubResRange };
  K3D_VK_VERIFY(
    vkCreateImageView(NativeDevice(), &m_ImageViewInfo, nullptr, &m_ResView));
}

VkImageCreateInfo ConvertFromTextureDesc(k3d::ResourceDesc const& ResDesc)
{
  auto Desc = ResDesc.TextureDesc;
  VkImageCreateInfo Info =
  {
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr,
  };
  switch (ResDesc.Type)
  {
  case k3d::EGT_Texture1D:
  case k3d::EGT_Texture1DArray:
    Info.imageType = VK_IMAGE_TYPE_1D;
    break;
  case k3d::EGT_Texture2DMSArray:
  case k3d::EGT_TextureCube:
  case k3d::EGT_Texture2DArray:
  case k3d::EGT_Texture2D:
    Info.imageType = VK_IMAGE_TYPE_2D;
    break;
  case k3d::EGT_Texture3D:
    Info.imageType = VK_IMAGE_TYPE_3D;
    break;
  }
  Info.format = g_FormatTable[Desc.Format];
  Info.extent = { Desc.Width, Desc.Height, Desc.Depth };
  Info.mipLevels = Desc.MipLevels;
  Info.arrayLayers = Desc.Layers;
  // TODO
  Info.samples = VK_SAMPLE_COUNT_1_BIT;
  // TODO
  Info.tiling = VK_IMAGE_TILING_OPTIMAL; //VK_IMAGE_TILING_LINEAR
  // TODO
  Info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  
  
  Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  Info.queueFamilyIndexCount = 0;
  Info.pQueueFamilyIndices = nullptr;
  Info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  return Info;
}

void
Texture::CreateRenderTexture(TextureDesc const& desc)
{
  m_ImageInfo = ConvertFromTextureDesc(m_ResDesc);
  m_MemoryBits =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  ThisResourceType::Allocate(m_ImageInfo);

  K3D_VK_VERIFY(vkBindImageMemory(NativeDevice(), m_NativeObj, m_DeviceMem, 0));

  m_ImageViewInfo = 
  {

  };
  K3D_VK_VERIFY(
    vkCreateImageView(NativeDevice(), &m_ImageViewInfo, nullptr, &m_ResView));

  VkImageSubresource subres;
  subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  subres.mipLevel = 0;   // temp use
  subres.arrayLayer = 0; // temp use
  // query texture memory layout info here
  // image must have been created with tiling equal to VK_IMAGE_TILING_LINEAR
  // The aspectMask member of pSubresource must only have a single bit set
  vkGetImageSubresourceLayout(
    NativeDevice(), m_NativeObj, &subres, &m_SubResourceLayout);
}

void
Texture::CreateDepthStencilTexture(TextureDesc const& desc)
{
}

void
Texture::CreateSampledTexture(TextureDesc const& desc)
{
  m_ImageInfo = ConvertFromTextureDesc(m_ResDesc);
  m_ImageInfo.usage = m_ResUsageFlags | VK_IMAGE_USAGE_SAMPLED_BIT;
  if (m_ResUsageFlags &
      VK_IMAGE_USAGE_TRANSFER_DST_BIT) // texture upload use staging
  {
    m_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    m_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  } else // directly upload
  {
    m_ImageInfo.tiling = VK_IMAGE_TILING_LINEAR;
    m_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
  }

  m_SubResRange = {
    VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.MipLevels, 0, desc.Layers
  };

  ThisResourceType::Allocate(m_ImageInfo);

  K3D_VK_VERIFY(vkBindImageMemory(NativeDevice(), m_NativeObj, m_DeviceMem, 0));
}

Texture::~Texture()
{
}

ShaderResourceView::ShaderResourceView(Device::Ptr pDevice,
                                       k3d::ResourceViewDesc const& desc,
                                       k3d::GpuResourceRef pGpuResource)
  : ShaderResourceView::ThisObj(pDevice)
  , m_Desc(desc)
  , m_WeakResource(pGpuResource)
  , m_TextureViewInfo{}
{
  auto resourceDesc = m_WeakResource->GetDesc();
  m_TextureViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  m_TextureViewInfo.components = { VK_COMPONENT_SWIZZLE_R,
                                   VK_COMPONENT_SWIZZLE_G,
                                   VK_COMPONENT_SWIZZLE_B,
                                   VK_COMPONENT_SWIZZLE_A };
  
  m_TextureViewInfo.viewType = ConvertFromTextureType(resourceDesc.Type);
  m_TextureViewInfo.format = g_FormatTable[resourceDesc.TextureDesc.Format];
  m_TextureViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  m_TextureViewInfo.subresourceRange.baseMipLevel = 0;
  m_TextureViewInfo.subresourceRange.baseArrayLayer = 0;
  m_TextureViewInfo.subresourceRange.layerCount = 1;
  m_TextureViewInfo.subresourceRange.levelCount =
    resourceDesc.TextureDesc.MipLevels;
  m_TextureViewInfo.image = (VkImage)m_WeakResource->GetLocation();

  K3D_VK_VERIFY(vkCreateImageView(
    NativeDevice(), &m_TextureViewInfo, nullptr, &m_NativeObj));
}

ShaderResourceView::~ShaderResourceView()
{
  // destroy view
  VKLOG(Info, "ShaderResourceView destroying...");
  if (m_NativeObj) {
    vkDestroyImageView(NativeDevice(), m_NativeObj, nullptr);
    m_NativeObj = VK_NULL_HANDLE;
  }
}

Sampler::Sampler(Device::Ptr pDevice, k3d::SamplerState const& samplerDesc)
  : ThisObj(pDevice)
  , m_SamplerState(samplerDesc)
{
  if (pDevice) {
    m_SamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    m_SamplerCreateInfo.magFilter = g_Filters[samplerDesc.Filter.MagFilter];
    m_SamplerCreateInfo.minFilter = g_Filters[samplerDesc.Filter.MinFilter];
    m_SamplerCreateInfo.mipmapMode =
      g_MipMapModes[samplerDesc.Filter.MipMapFilter];
    m_SamplerCreateInfo.addressModeU = g_AddressModes[samplerDesc.U];
    m_SamplerCreateInfo.addressModeV = g_AddressModes[samplerDesc.V];
    m_SamplerCreateInfo.addressModeW = g_AddressModes[samplerDesc.W];
    m_SamplerCreateInfo.mipLodBias = samplerDesc.MipLODBias;
    m_SamplerCreateInfo.compareOp =
      g_ComparisonFunc[samplerDesc.ComparisonFunc];
    m_SamplerCreateInfo.minLod = samplerDesc.MinLOD;
    // Max level-of-detail should match mip level count
    m_SamplerCreateInfo.maxLod = samplerDesc.MaxLOD;
    // Enable anisotropic filtering
    m_SamplerCreateInfo.maxAnisotropy = 1.0f * samplerDesc.MaxAnistropy;
    m_SamplerCreateInfo.anisotropyEnable = VK_TRUE;
    m_SamplerCreateInfo.borderColor =
      VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // cannot convert...
    vkCreateSampler(
      NativeDevice(), &m_SamplerCreateInfo, nullptr, &m_NativeObj);
  }
}

Sampler::~Sampler()
{
  VKLOG(Info, "Sampler Destroying %p...", m_NativeObj);
  vkDestroySampler(NativeDevice(), m_NativeObj, nullptr);
}

k3d::SamplerState
Sampler::GetSamplerDesc() const
{
  return m_SamplerState;
}

template<typename VkObject>
ResourceManager::Allocation
ResourceManager::Pool<VkObject>::Allocate(
  const typename ResourceManager::ResDesc<VkObject>& objDesc)
{
  ResourceManager::Allocation result = {};
  const VkMemoryRequirements& memReqs = objDesc.MemoryRequirements;
  if (HasAvailable(memReqs)) {
    const VkDeviceSize initialOffset = m_Offset;
    const VkDeviceSize alignedOffset =
      CalcAlignedOffset(initialOffset, memReqs.alignment);
    const VkDeviceSize allocatedSize = memReqs.size;
    result.Memory = m_Memory;
    result.Offset = alignedOffset;
    result.Size = allocatedSize;
    m_Allocations.push_back(result);
    m_Offset += allocatedSize;
  }
  return result;
}

template<typename VkObjectT>
ResourceManager::Pool<VkObjectT>::Pool(uint32 memTypeIndex,
                                       VkDeviceMemory mem,
                                       VkDeviceSize sz)
  : m_MemoryTypeIndex(memTypeIndex)
  , m_Memory(mem)
  , m_Size(sz)
{
}

template<typename VkObject>
std::unique_ptr<ResourceManager::Pool<VkObject>>
ResourceManager::Pool<VkObject>::Create(
  VkDevice device,
  const VkDeviceSize poolSize,
  const typename ResourceManager::ResDesc<VkObject>& objDesc)
{
  std::unique_ptr<ResourceManager::Pool<VkObject>> result;
  const uint32_t memoryTypeIndex = objDesc.MemoryTypeIndex;
  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.pNext = nullptr;
  allocInfo.allocationSize = poolSize;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkResult res = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
  if (VK_SUCCESS == res) {
    result = std::unique_ptr<ResourceManager::Pool<VkObject>>(
      new ResourceManager::Pool<VkObject>(memoryTypeIndex, memory, poolSize));
  }
  return result;
}

template<typename VkObjectT>
bool
ResourceManager::Pool<VkObjectT>::HasAvailable(
  VkMemoryRequirements memReqs) const
{
  VkDeviceSize alignedOffst = CalcAlignedOffset(m_Offset, memReqs.alignment);
#ifdef min
#undef min
#endif
  VkDeviceSize remaining = m_Size - std::min(alignedOffst, m_Size);
  return memReqs.size <= remaining;
}

template<typename VkObjectT>
ResourceManager::PoolManager<VkObjectT>::PoolManager(Device::Ptr pDevice,
                                                     VkDeviceSize poolSize)
  : DeviceChild(pDevice)
  , m_PoolSize(poolSize)
{
}

template<typename VkObjectT>
ResourceManager::PoolManager<VkObjectT>::~PoolManager()
{
  Destroy();
}

template<typename VkObjectT>
void
ResourceManager::PoolManager<VkObjectT>::Destroy()
{
  if (!GetRawDevice())
    return;
  ::Os::Mutex::AutoLock lock(&m_Mutex);
  for (auto& pool : m_Pools) {
    vkFreeMemory(GetRawDevice(), pool->m_Memory, nullptr);
  }
  m_Pools.clear();
}

template<typename VkObjectT>
ResourceManager::Allocation
ResourceManager::PoolManager<VkObjectT>::Allocate(
  const typename ResourceManager::ResDesc<VkObjectT>& objDesc)
{
  ::Os::Mutex::AutoLock lock(&m_Mutex);
  ResourceManager::Allocation result;
  if (objDesc.MemoryRequirements.size > m_PoolSize) {
    auto pool = ResourceManager::Pool<VkObjectT>::Create(
      GetRawDevice(), objDesc.MemoryRequirements.size, objDesc);
    if (pool) {
      result = pool->Allocate(objDesc);
      m_Pools.push_back(std::move(pool));
    }
  } else {
    // Look to see if there's a pool that fits the requirements...
    auto it = std::find_if(
      std::begin(m_Pools),
      std::end(m_Pools),
      [objDesc](
        const ResourceManager::PoolManager<VkObjectT>::PoolRef& elem) -> bool {
        bool isMemoryType =
          (elem->GetMemoryTypeIndex() == objDesc.MemoryTypeIndex);
        bool hasSpace = elem->HasAvailable(objDesc.MemoryRequirements);
        return isMemoryType && hasSpace;
      });

    // ...if there is allocate from the available pool
    if (std::end(m_Pools) != it) {
      auto& pool = *it;
      result = pool->Allocate(objDesc);
    }
    // ...otherwise create a new pool and allocate from it
    else {
      auto pool = ResourceManager::Pool<VkObjectT>::Create(
        GetRawDevice(), m_PoolSize, objDesc);
      if (pool) {
        result = pool->Allocate(objDesc);
        m_Pools.push_back(std::move(pool));
      }
    }
  }
  return result;
}

ResourceManager::ResourceManager(Device::Ptr pDevice,
                                 size_t bufferBlockSize,
                                 size_t imageBlockSize)
  : DeviceChild(pDevice)
  , m_BufferAllocations(pDevice, bufferBlockSize)
  , m_ImageAllocations(pDevice, imageBlockSize)
{
  Initialize();
}

ResourceManager::~ResourceManager()
{
  Destroy();
}

ResourceManager::Allocation
ResourceManager::AllocateBuffer(VkBuffer buffer,
                                bool transient,
                                VkMemoryPropertyFlags memoryProperty)
{
  VkMemoryRequirements memoryRequirements = {};
  vkGetBufferMemoryRequirements(GetRawDevice(), buffer, &memoryRequirements);
  uint32_t memoryTypeIndex = 0;
  bool foundMemory = GetDevice()->FindMemoryType(
    memoryRequirements.memoryTypeBits, memoryProperty, &memoryTypeIndex);
  K3D_ASSERT(foundMemory);
  ResourceManager::ResDesc<VkBuffer> objDesc = {};
  objDesc.Object = buffer;
  objDesc.IsTransient = transient;
  objDesc.MemoryTypeIndex = memoryTypeIndex;
  objDesc.MemoryProperty = memoryProperty;
  objDesc.MemoryRequirements = memoryRequirements;
  return m_BufferAllocations.Allocate(objDesc);
}

ResourceManager::Allocation
ResourceManager::AllocateImage(VkImage image,
                               bool transient,
                               VkMemoryPropertyFlags memoryProperty)
{
  VkMemoryRequirements memoryRequirements = {};
  vkGetImageMemoryRequirements(GetRawDevice(), image, &memoryRequirements);
  uint32_t memoryTypeIndex = 0;
  bool foundMemory = GetDevice()->FindMemoryType(
    memoryRequirements.memoryTypeBits, memoryProperty, &memoryTypeIndex);
  K3D_ASSERT(foundMemory);
  ResourceManager::ResDesc<VkImage> objDesc = {};
  objDesc.Object = image;
  objDesc.IsTransient = transient;
  objDesc.MemoryTypeIndex = memoryTypeIndex;
  objDesc.MemoryProperty = memoryProperty;
  objDesc.MemoryRequirements = memoryRequirements;
  return m_ImageAllocations.Allocate(objDesc);
}

void
ResourceManager::Initialize()
{
}

void
ResourceManager::Destroy()
{
  m_BufferAllocations.Destroy();
  m_ImageAllocations.Destroy();
}

K3D_VK_END