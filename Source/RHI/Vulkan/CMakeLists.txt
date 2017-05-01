file(GLOB VKRHI_SRC_LIST "*.cpp" "*.h")
include_directories(
../.. ../../../Include
.
)

if(WIN32)
	add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
    generate_win_res(RHI_Vulkan VER "1.0.0.1" DESC "Vulkan RHI Module")
elseif(ANDROID)
	add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR)
endif()

set(RHI_HEADERS
	VkCommon.h
	Public/IVkRHI.h
)

set(RHI_IMPL_SRCS
	Private/VkConfig.h
	Private/VkRHI.h
	Private/DynVulkanLoader.h
	Private/VkEnums.h
	Private/VkResource.cpp
	Private/VkImpl.cpp
	Private/DynVulkanLoader.cpp
)

if(WIN32)
	list(APPEND RHI_IMPL_SRCS ${RHI_Vulkan_RC})
endif(WIN32)

source_group(
	Public FILES
	${RHI_HEADERS}
)

source_group(
	Private FILES
	${RHI_IMPL_SRCS}
)

source_group(
	Config FILES
	Private/VkConfig.h
	${RHI_Vulkan_RC}
)

add_plugin(RHI_Vulkan 
	FOLDER "Runtime/RHI" 
	SRCS ${VKRHI_SRC_LIST} ${RHI_HEADERS} ${RHI_IMPL_SRCS} 
	LIBS RHIUtil ShaderCompiler ${VULKAN_LIB})

if(NOT ANDROID)
	add_precompiled_header(RHI_Vulkan "VkCommon.h" "${Kaleido3D_ROOT_DIR}/Source/RHI/Vulkan" "${Kaleido3D_ROOT_DIR}/Source/RHI/Vulkan/Private/VkImpl.cpp")
endif()

install(TARGETS RHI_Vulkan 
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)