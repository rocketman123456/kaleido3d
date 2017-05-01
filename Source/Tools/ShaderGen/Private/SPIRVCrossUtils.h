#pragma once
#include <Interface/ICrossShaderCompiler.h>
#include <spirv2cross/spirv_cross.hpp>
#include <spirv2cross/spirv_msl.hpp>
#include <spirv2cross/spirv_glsl.hpp>

k3d::shc::EDataType spirTypeToRHIAttribType(const spirv_cross::SPIRType& spirType);
k3d::shc::EDataType spirTypeToGlslUniformDataType(const spirv_cross::SPIRType& spirType);
spv::ExecutionModel rhiShaderStageToSpvModel(k3d::EShaderType const& type);
void ExtractAttributeData(spirv_cross::CompilerGLSL const& backCompiler, k3d::shc::Attributes & outShaderAttributes);
void ExtractUniformData(k3d::EShaderType shaderStage, spirv_cross::CompilerGLSL const& backCompiler, k3d::shc::BindingTable& outUniformLayout);
