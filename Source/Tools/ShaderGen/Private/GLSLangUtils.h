#pragma once
#include <Interface/ICrossShaderCompiler.h>
#include <glslang/GlslangToSpv.h>

void sInitializeGlSlang();
void sFinializeGlSlang();

void initResources(TBuiltInResource &resources);
EShLanguage findLanguage(const k3d::EShaderType shader_type);
k3d::shc::EDataType glTypeToRHIAttribType(int glType);
k3d::shc::EDataType glTypeToRHIUniformType(int glType);
k3d::shc::EDataType glslangDataTypeToRHIDataType(const glslang::TType& type);
k3d::shc::EBindType glslangTypeToRHIType(const glslang::TBasicType& type);

void ExtractAttributeData(const glslang::TProgram& program, k3d::shc::Attributes& shAttributes);
void ExtractUniformData(k3d::EShaderType const& type, const glslang::TProgram& program, k3d::shc::BindingTable& outUniformLayout);
