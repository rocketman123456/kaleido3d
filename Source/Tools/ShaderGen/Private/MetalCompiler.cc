#include <Kaleido3D.h>
#include "MetalCompiler.h"
#include <Core/Utils/MD5.h>
#include <Core/Os.h>
#include "GLSLangUtils.h"
#include "SPIRVCrossUtils.h"

#define METAL_BIN_DIR_MACOS "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/usr/bin/"
#define METAL_BIN_DIR_IOS "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/usr/bin/"
#define METAL_COMPILE_TMP "/.metaltmp/"
#define COMPILE_OPTION "-arch air64 -emit-llvm -c"

#include <string.h>

using namespace std;
using namespace k3d::shc;

namespace k3d
{
    EResult mtlCompile(string const& source, String & metalIR);

    MetalCompiler::MetalCompiler()
    {
        sInitializeGlSlang();
    }
    
    MetalCompiler::~MetalCompiler()
    {
        sFinializeGlSlang();
    }
    
    EResult MetalCompiler::Compile(String const& src, k3d::ShaderDesc const& inOp, k3d::ShaderBundle & bundle)
    {
        if(inOp.Format == k3d::EShFmt_Text)
        {
            if(inOp.Lang == k3d::EShLang_MetalSL)
            {
                if(m_IsMac)
                {
                    
                }
                else // iOS
                {
                    
                }
            }
            else // process hlsl or glsl
            {
                bool canConvertToMetalSL = false;
                switch (inOp.Lang) {
                    case k3d::EShLang_ESSL:
                    case k3d::EShLang_GLSL:
                    case k3d::EShLang_HLSL:
                    case k3d::EShLang_VkGLSL:
                        canConvertToMetalSL = true;
                        break;
                    default:
                        break;
                }
                if (canConvertToMetalSL)
                {
                    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
                    switch(inOp.Lang)
                    {
                        case k3d::EShLang_ESSL:
                        case k3d::EShLang_GLSL:
                        case k3d::EShLang_VkGLSL:
                            break;
                        case k3d::EShLang_HLSL:
                            messages = (EShMessages)(EShMsgVulkanRules | EShMsgSpvRules | EShMsgReadHlsl);
                            break;
                        default:
                            break;
                    }
                    glslang::TProgram& program = *new glslang::TProgram;
                    TBuiltInResource Resources;
                    initResources(Resources);
                    
                    const char *shaderStrings[1];
                    EShLanguage stage = findLanguage(inOp.Stage);
                    glslang::TShader* shader = new glslang::TShader(stage);
                    
                    shaderStrings[0] = src.CStr();
                    shader->setStrings(shaderStrings, 1);
                    shader->setEntryPoint(inOp.EntryFunction.CStr());
                    
                    if (!shader->parse(&Resources, 100, false, messages)) {
                        puts(shader->getInfoLog());
                        puts(shader->getInfoDebugLog());
                        return k3d::shc::E_Failed;
                    }
                    program.addShader(shader);
                    if (!program.link(messages)) {
                        puts(program.getInfoLog());
                        puts(program.getInfoDebugLog());
                        return k3d::shc::E_Failed;
                    }
                    std::vector<unsigned int> spirv;
                    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);

                    if(program.buildReflection())
                    {
                        ExtractAttributeData(program, bundle.Attributes);
                        ExtractUniformData(inOp.Stage, program, bundle.BindingTable);
                    }
                    else
                    {
                        return k3d::shc::E_Failed;
                    }
                    uint32 bufferLoc = 0;
                    std::vector<spirv_cross::MSLVertexAttr> vertAttrs;
                    for(auto & attr : bundle.Attributes)
                    {
                        spirv_cross::MSLVertexAttr vAttrib;
                        vAttrib.location = attr.VarLocation;
                        vAttrib.msl_buffer = attr.VarBindingPoint;
                        vertAttrs.push_back(vAttrib);
                        bufferLoc = attr.VarBindingPoint;
                    }
                    std::vector<spirv_cross::MSLResourceBinding> resBindings;
                    for(auto & binding : bundle.BindingTable.Bindings)
                    {
                        if(binding.VarType == EBindType::EBlock)
                        {
                            bufferLoc ++;
                            spirv_cross::MSLResourceBinding resBind;
                            resBind.stage = rhiShaderStageToSpvModel(binding.VarStage);
                            resBind.desc_set = 0;
                            resBind.binding = binding.VarNumber;
                            resBind.msl_buffer = bufferLoc;
                            resBindings.push_back(resBind);
                        }
                    }
                    auto metalc = make_unique<spirv_cross::CompilerMSL>(spirv);
                    spirv_cross::MSLConfiguration config;
                    config.flip_vert_y = false;
                    config.flip_frag_y = false;
                    config.entry_point_name = inOp.EntryFunction.CStr();
                    auto result = metalc->compile(config, &vertAttrs, &resBindings);
                    if(m_IsMac)
                    {
                        auto ret = mtlCompile(result, bundle.RawData);
                        if(ret == E_Failed)
                            return ret;
                        bundle.Desc = inOp;
                        bundle.Desc.Format = k3d::EShFmt_ByteCode;
                        bundle.Desc.Lang = k3d::EShLang_MetalSL;
                    }
                    else
                    {
                        bundle.RawData = { result.c_str() };
                        bundle.Desc = inOp;
                        bundle.Desc.Format = k3d::EShFmt_Text;
                        bundle.Desc.Lang = k3d::EShLang_MetalSL;
                    }
                }
            }
        }
        else
        {
            if(inOp.Lang == k3d::EShLang_MetalSL)
            {
                
            }
            else
            {
                
            }
        }
        return E_Ok;
    }
    
    const char*
    MetalCompiler::GetVersion()
    {
        return "Metal";
    }
    
    EResult mtlCompile(string const& source, String & metalIR)
    {
#if K3DPLATFORM_OS_MAC 
        MD5 md5(source);
        auto name = md5.toString();
        auto intermediate = string(".") + METAL_COMPILE_TMP;
        Os::MakeDir(intermediate.c_str());
        auto tmpSh = intermediate + name + ".metal";
        auto tmpAr = intermediate + name + ".air";
        auto tmpLib = intermediate + name + ".metallib";
        Os::File shSrcFile(tmpSh.c_str());
        shSrcFile.Open(IOWrite);
        shSrcFile.Write(source.data(), source.size());
        shSrcFile.Close();
        auto mcc = string(METAL_BIN_DIR_MACOS) + "metal";
        auto mlink = string(METAL_BIN_DIR_MACOS) + "metallib";
        auto ccmd = mcc + " -arch air64 -c -o " + tmpAr + " " + tmpSh;
        auto ret = system(ccmd.c_str());
        if(ret)
        {
            return E_Failed;
        }
        auto lcmd = mlink + " -split-module -o " + tmpLib + " " + tmpAr;
        ret = system(lcmd.c_str());
        if(ret)
        {
            return E_Failed;
        }
        Os::MemMapFile bcFile;
        bcFile.Open(tmpLib.c_str(), IORead);
        metalIR = { bcFile.FileData(), (size_t)bcFile.GetSize() };
        bcFile.Close();
        //Os::Remove(intermediate.c_str());
#endif
        return E_Ok;
    }
}
