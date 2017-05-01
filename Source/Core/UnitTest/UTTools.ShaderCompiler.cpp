#include "Common.h"
#include <Core/Module.h>
#include <iostream>

#if K3DPLATFORM_OS_WIN
#pragma comment(linker,"/subsystem:console")
#endif

using namespace std;
using namespace k3d;

void TestShaderCompiler()
{
    Os::MemMapFile vShFile;
    vShFile.Open("../../Data/Test/testCompiler.glsl", IOFlag::IORead);
    String vertexSource((const char*)vShFile.FileData());
    vShFile.Close();
        
	auto shMod = StaticPointerCast<k3d::IShModule>(GlobalModuleManager.FindModule("ShaderCompiler"));
	if (shMod)
	{
		// test compile
		SharedPtr<k3d::IShCompiler> vkCompiler = shMod->CreateShaderCompiler(k3d::ERHI_Vulkan);
		if (vkCompiler)
		{
			k3d::ShaderDesc desc = { k3d::EShFmt_Text, k3d::EShLang_GLSL, k3d::EShProfile_Modern, k3d::ES_Vertex, "main" };
			k3d::ShaderBundle bundle;
			auto ret = vkCompiler->Compile(vertexSource, desc, bundle);
			K3D_ASSERT(ret == k3d::shc::E_Ok);

			// test shader serialize
			Os::File shBundle;
			shBundle.Open(KT("shaderbundle.sh"), IOWrite);
			Archive ar;
			ar.SetIODevice(&shBundle);
			ar << bundle;
			shBundle.Close();

			// write spirv to file
			Os::File spirvFile;
			spirvFile.Open(KT("triangle.spv"), IOWrite);
			spirvFile.Write(bundle.RawData.Data(), bundle.RawData.Length());
			spirvFile.Close();

			// test shader deserialize;
			Os::File shBundleRead;
			k3d::ShaderBundle bundleRead;
			shBundleRead.Open(KT("shaderbundle.sh"), IORead);
			Archive readar;
			readar.SetIODevice(&shBundleRead);
			readar >> bundleRead;
			shBundleRead.Close();

			// test hlsl compile
			k3d::ShaderBundle hlslBundle;
			Os::MemMapFile hlslVertFile;
			hlslVertFile.Open("../../Data/Test/TestMaterial.hlsl", IOFlag::IORead);
			k3d::ShaderDesc hlsldesc = { k3d::EShFmt_Text, k3d::EShLang_HLSL, k3d::EShProfile_Modern, k3d::ES_Vertex, "main" };
			auto hlslret = vkCompiler->Compile(String((const char*)hlslVertFile.FileData()), hlsldesc, hlslBundle);
			K3D_ASSERT(hlslret == k3d::shc::E_Ok);

			// test spirv reflect
			Os::MemMapFile spirvFileRead;
			k3d::ShaderBundle spirvBundle;
			spirvFileRead.Open(KT("triangle.spv"), IORead);
			k3d::ShaderDesc spirvDesc = { k3d::EShFmt_ByteCode, k3d::EShLang_GLSL, k3d::EShProfile_Modern, k3d::ES_Vertex, "main" };
			auto spirvRet = vkCompiler->Compile(String(spirvFileRead.FileData(), spirvFileRead.GetSize()), spirvDesc, spirvBundle);
			K3D_ASSERT(spirvRet == k3d::shc::E_Ok);
		}
            
        SharedPtr<k3d::IShCompiler> mtlCompiler = shMod->CreateShaderCompiler(k3d::ERHI_Metal);
        if(mtlCompiler)
        {
            k3d::ShaderDesc desc = { k3d::EShFmt_Text, k3d::EShLang_GLSL, k3d::EShProfile_Modern, k3d::ES_Vertex, "main" };
            k3d::ShaderBundle bundle;
            auto ret = mtlCompiler->Compile(vertexSource, desc, bundle);
            K3D_ASSERT(ret == k3d::shc::E_Ok);
                
            // test shader serialize
            Os::File shBundle;
            shBundle.Open(KT("metalshaderbundle.sh"), IOWrite);
            Archive ar;
            ar.SetIODevice(&shBundle);
            ar << bundle;
            shBundle.Close();
        }
	}
}

int main(int argc, char**argv)
{
	TestShaderCompiler();
	return 0;
}