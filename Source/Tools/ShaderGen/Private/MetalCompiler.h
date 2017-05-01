#pragma once

#include "Public/ShaderCompiler.h"

namespace k3d
{
    class MetalCompiler : public k3d::IShCompiler
    {
    public:
        MetalCompiler();
        ~MetalCompiler() override;
        k3d::shc::EResult	Compile(String const& src,
                                    k3d::ShaderDesc const& inOp,
                                    k3d::ShaderBundle & bundle) override;
        const char *        GetVersion();
    
    private:
        bool m_IsMac = true;
    };
}
