#include "Kaleido3D.h"
#include "FontRenderer.h"
#include <Core/Module.h>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace render
{
	FontManager::FontManager()
		: m_PixelSize(0)
		, m_Color(0xffffffff)
		, m_pFontLib(nullptr)
		, m_pFontFace(nullptr)
	{
		FT_Init_FreeType((FT_Library*)&m_pFontLib);
	}

	FontManager::~FontManager()
	{
		if (m_pFontFace)
		{
			FT_Done_Face((FT_Face)m_pFontFace);
			m_pFontFace = nullptr;
		}
		if (m_pFontLib)
		{
			FT_Done_FreeType((FT_Library)m_pFontLib);
			m_pFontLib = nullptr;
		}
	}

	void FontManager::LoadLib(const char * fontPath)
	{
		if (m_pFontFace)
		{
			FT_Done_Face((FT_Face)m_pFontFace);
			m_pFontFace = nullptr;
		}
		FT_New_Face((FT_Library)m_pFontLib, fontPath, 0, (FT_Face*)&m_pFontFace);
	}

	void FontManager::ChangeFontSize(int height)
	{
		FT_Set_Pixel_Sizes((FT_Face)m_pFontFace, 0, height);
		m_PixelSize = height;
	}

	void FontManager::SetPaintColor(int color)
	{
		m_Color = color;
	}

	unsigned int* GlyphTexture(const FT_Bitmap& bitmap, const unsigned int& color)
	{
		unsigned int* buffer = new unsigned int[bitmap.width * bitmap.rows * 4];
		for (int y = 0; y< bitmap.rows; y++)
		{
			for (int x = 0; x < bitmap.width; x++)
			{
				unsigned int pixel = (color & 0xffffff00);
				unsigned int alpha = bitmap.buffer[(y * bitmap.pitch) + x];
				pixel |= alpha;
				buffer[(y*bitmap.width) + x] = pixel;
			}
		}
		return buffer;
	}

	TextQuads FontManager::AcquireText(const::k3d::String & text)
	{
		if(text.Length()==0 || !m_pFontFace)
			return TextQuads();
		FT_Face face = (FT_Face)m_pFontFace;
		TextQuads quadlist;
		for (unsigned int i = 0; i < text.Length(); i++) 
		{
			FT_Load_Char(face, text[i], FT_LOAD_RENDER | FT_LOAD_NO_HINTING);
			unsigned int *bytes = GlyphTexture(face->glyph->bitmap, m_Color);
			int width = face->glyph->bitmap.width;
			int height = face->glyph->bitmap.rows; 
			int x = face->glyph->metrics.horiBearingX / m_PixelSize;
			int y =	face->glyph->metrics.horiBearingY / m_PixelSize;
			int next = face->glyph->metrics.horiAdvance / m_PixelSize;
			quadlist.Append({ x,y, width, height, next, bytes});
		}
		return quadlist;
	}

	CharTexture::CharTexture(k3d::DeviceRef device, TextQuad const & quad)
	{
		k3d::ResourceDesc texDesc;
		texDesc.Type = k3d::EGT_Texture2D;
		texDesc.ViewType = k3d::EGpuMemViewType::EGVT_SRV;
		texDesc.Flag = k3d::EGpuResourceAccessFlag::EGRAF_HostVisible;
		texDesc.TextureDesc.Format = k3d::EPF_RGBA8Unorm; // TODO font color fmt inconsistent
		texDesc.TextureDesc.Width = quad.W;
		texDesc.TextureDesc.Height = quad.H;
		texDesc.TextureDesc.Layers = 1;
		texDesc.TextureDesc.MipLevels = 1;
		texDesc.TextureDesc.Depth = 1;
		m_Texture = ::k3d::DynamicPointerCast<k3d::ITexture>(device->NewGpuResource(texDesc));

		uint64 sz = m_Texture->GetSize();
		void * pData = m_Texture->Map(0, sz);
		k3d::SubResourceLayout layout = {};
		k3d::TextureResourceSpec spec = { k3d::ETAF_COLOR,0,0 };
		device->QueryTextureSubResourceLayout(m_Texture, spec, &layout);
		if (quad.W * 4 == layout.RowPitch)
		{
			memcpy(pData, quad.Pixels, sz);
		}
		else
		{
			for (int y = 0; y < quad.H; y++)
			{
				uint32_t *row = (uint32_t *)((char *)pData + layout.RowPitch * y);
				for (int x = 0; x < quad.W; x++)
				{
					row[x] = quad.Pixels[x + y * quad.W];
				}
			}
		}
		m_Texture->UnMap();
#if 0
		auto cmd = device->NewCommandContext(k3d::ECMD_Graphics);
		cmd->Begin();
		cmd->TransitionResourceBarrier(m_Texture, k3d::ERS_ShaderResource);
		cmd->End();
		cmd->Execute(false);
#endif
	}
	
	short CharRenderer::s_Indices[] = { 0, 1, 3, 2 };
	
	float CharRenderer::s_Vertices[] = { 
		0.0f, 0.0f, 0.0f,	0.0f, 1.0f, // 0
		1.0f, 0.0f, 0.0f,	1.0f, 1.0f, // 1
		0.0f, 1.0f, 0.0f,	0.0f, 0.0f, // 3
		1.0f, 1.0f, 0.0f,	1.0f, 0.0f	// 2
	};

	CharRenderer::CharRenderer()
	{
	}

	CharRenderer::~CharRenderer()
	{
	}
	

	void CharRenderer::InitVertexBuffers(k3d::DeviceRef const & device)
	{
		k3d::ResourceDesc vboDesc;
		vboDesc.ViewType = k3d::EGpuMemViewType::EGVT_VBV;
		vboDesc.Flag = (k3d::EGpuResourceAccessFlag) (k3d::EGpuResourceAccessFlag::EGRAF_HostCoherent | k3d::EGpuResourceAccessFlag::EGRAF_HostVisible);
		vboDesc.Size = sizeof(s_Vertices);
		m_VertexBuffer = device->NewGpuResource(vboDesc);
		void * ptr = m_VertexBuffer->Map(0, vboDesc.Size);
		memcpy(ptr, s_Vertices, vboDesc.Size);
		m_VertexBuffer->UnMap();

		k3d::ResourceDesc iboDesc;
		iboDesc.ViewType = k3d::EGpuMemViewType::EGVT_IBV;
		iboDesc.Flag = (k3d::EGpuResourceAccessFlag) (k3d::EGpuResourceAccessFlag::EGRAF_HostCoherent | k3d::EGpuResourceAccessFlag::EGRAF_HostVisible);
		iboDesc.Size = sizeof(s_Indices);
		m_IndexBuffer = device->NewGpuResource(iboDesc);
		ptr = m_IndexBuffer->Map(0, iboDesc.Size);
		memcpy(ptr, s_Indices, iboDesc.Size);
		m_IndexBuffer->UnMap();
	}
	
	CharTexture::~CharTexture()
	{
	}

	FontRenderer::FontRenderer(k3d::DeviceRef const& device)
		: m_Device(device)
	{
	}
	
	FontRenderer::~FontRenderer()
	{
	}

	void FontRenderer::InitPSO()
	{
		auto shMod = k3d::StaticPointerCast<k3d::IShModule>(ACQUIRE_PLUGIN(ShaderCompiler));
		if (!shMod)
			return;
		auto glslc = shMod->CreateShaderCompiler(k3d::ERHI_Vulkan);
	}
	
}