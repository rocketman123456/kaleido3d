#ifndef __VkConfig_h__
#define __VkConfig_h__
#pragma once

#include <Math/kMath.hpp>

K3D_VK_BEGIN

namespace {
int g_ValidationLayerCount = 1;

const char* g_ValidationLayerNames[] = {
  "VK_LAYER_LUNARG_standard_validation",
};
}

K3D_VK_END
#endif