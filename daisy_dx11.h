// daisy_dx11 - DirectX 11 port of the daisy library
// - ported to DirectX 11 while preserving the original API design
// - original library by munteanu octavian-adrian (https://github.com/sse2/daisy)
// - MIT license

#ifndef _SSE2_DAISY_DX11_INCLUDE_GUARD
#define _SSE2_DAISY_DX11_INCLUDE_GUARD

#ifndef DAISY_NO_STL
#include <unordered_map>
#include <string_view>
#include <vector>
#include <array>
#include <string>
#include <string_view>
#include <atomic>
#include <memory>
#include <cstdint>
namespace stl = std;
#endif

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace daisy
{
	struct color_t
	{
		union
		{
			struct
			{
				uint8_t b, g, r, a;
			} chan;

			uint32_t bgra;
		};

		uint8_t& r( )
		{
			return chan.r;
		}

		uint8_t& g( )
		{
			return chan.g;
		}

		uint8_t& b( )
		{
			return chan.b;
		}

		uint8_t& a( )
		{
			return chan.a;
		}

		constexpr color_t( uint8_t _r = 255, uint8_t _g = 255, uint8_t _b = 255, uint8_t _a = 255 ) noexcept
			: chan( { _b, _g, _r, _a } )
		{
		}

		static color_t from_hsv( float hue, float saturation, float variance )
		{
			color_t ret;
			float C = saturation * variance;
			float X = C * ( 1 - fabsf( fmodf( hue / 60.f, 2 ) - 1 ) );
			float m = variance - C;

			float r, g, b;

			if ( hue >= 0.f && hue < 60.f )
				r = C, g = X, b = 0.f;
			else if ( hue >= 60.f && hue < 120.f )
				r = X, g = C, b = 0.f;
			else if ( hue >= 120.f && hue < 180 )
				r = 0.f, g = C, b = X;
			else if ( hue >= 180.f && hue < 240.f )
				r = 0.f, g = X, b = C;
			else if ( hue >= 240.f && hue < 300.f )
				r = X, g = 0.f, b = C;
			else
				r = C, g = 0.f, b = X;

			ret.chan.r = static_cast< unsigned char >( ( r + m ) * 255 );
			ret.chan.g = static_cast< unsigned char >( ( g + m ) * 255 );
			ret.chan.b = static_cast< unsigned char >( ( b + m ) * 255 );
			ret.chan.a = 255;

			return ret;
		}

		static void rgb_to_hsv( const color_t& c, float* hsv )
		{
			float r = c.chan.r / 255.f;
			float g = c.chan.g / 255.f;
			float b = c.chan.b / 255.f;

			float cmax = max( r, max( g, b ) );
			float cmin = min( r, min( g, b ) );
			float diff = cmax - cmin;

			float h = 0.f, s = 0.f, v = cmax;

			if ( diff > 0.f )
			{
				if ( cmax == r )
					h = fmod( 60 * ( ( g - b ) / diff ) + 360, 360 );
				else if ( cmax == g )
					h = fmod( 60 * ( ( b - r ) / diff ) + 120, 360 );
				else if ( cmax == b )
					h = fmod( 60 * ( ( r - g ) / diff ) + 240, 360 );

				if ( cmax > 0.f )
					s = diff / cmax;
			}

			hsv[ 0 ] = h;
			hsv[ 1 ] = s;
			hsv[ 2 ] = v;
		}

		color_t lerp( const color_t& other, float t ) const
		{
			return color_t(
				this->chan.r + ( other.chan.r - this->chan.r ) * t,
				this->chan.g + ( other.chan.g - this->chan.g ) * t,
				this->chan.b + ( other.chan.b - this->chan.b ) * t,
				this->chan.a + ( other.chan.a - this->chan.a ) * t
			);
		}
	};

	// Vertex struct - modified for D3D11
	struct daisy_vtx_t
	{
		float m_pos[ 4 ]; // x,y,z,w (was rhw in D3D9)
		uint32_t m_col; // RGBA color
		float m_uv[ 2 ];
	};

	// Buffer structure
	struct renderbuffer_t
	{
		stl::unique_ptr<uint8_t[ ]> m_data { nullptr };
		uint32_t m_capacity { 0 }, m_size { 0 };
	};

	enum class daisy_call_kind : uint8_t
	{
		CALL_TRI = 0,
		CALL_VTXSHADER,
		CALL_PIXSHADER,
		CALL_SCISSOR
	};

	enum daisy_text_align : uint16_t
	{
		TEXT_ALIGN_DEFAULT = 0,
		TEXT_ALIGNX_LEFT = 1 << 0,
		TEXT_ALIGNX_CENTER = 1 << 1,
		TEXT_ALIGNX_RIGHT = 1 << 2,
		TEXT_ALIGNY_TOP = 1 << 3,
		TEXT_ALIGNY_CENTER = 1 << 4,
		TEXT_ALIGNY_BOTTOM = 1 << 5,
	};

	enum daisy_font_flags : uint8_t
	{
		FONT_DEFAULT = 0,
		FONT_BOLD = 1 << 0,
		FONT_ITALIC = 1 << 1
	};

	using uv_t = stl::array<float, 4>;

	struct point_t
	{
		point_t( )
		{
		}
		point_t( float x, float y ) : x( x ), y( y )
		{
		}
		point_t( int x, int y ) : x( ( float ) x ), y( ( float ) y )
		{
		}

		float x { 0.f }, y { 0.f };
	};

	struct daisy_drawcall_t
	{
		daisy_call_kind m_kind;

		union
		{
			struct
			{
				ID3D11ShaderResourceView* m_texture_srv;
				uint32_t m_primitives, m_vertices, m_indices;
			} m_tri;

			struct
			{
				void* m_shader_handle;
			} m_shader;

			struct
			{
				point_t m_position, m_size;
			} m_scissor;
		};
	};

	// Global D3D11 state
	struct daisy_t
	{
		static inline ID3D11Device* s_device = nullptr;
		static inline ID3D11DeviceContext* s_context = nullptr;

		// Shader resources
		static inline ID3D11VertexShader* s_vertex_shader = nullptr;
		static inline ID3D11PixelShader* s_pixel_shader = nullptr;
		static inline ID3D11InputLayout* s_input_layout = nullptr;
		static inline ID3D11BlendState* s_blend_state = nullptr;
		static inline ID3D11RasterizerState* s_rasterizer_state = nullptr;
		static inline ID3D11DepthStencilState* s_depth_stencil_state = nullptr;
		static inline ID3D11SamplerState* s_sampler_state = nullptr;

		// Default white texture for non-textured rendering
		static inline ID3D11Texture2D* s_white_texture = nullptr;
		static inline ID3D11ShaderResourceView* s_white_texture_srv = nullptr;

		// Viewport dimensions (needed for vertex shader)
		static inline float s_viewport_width = 1920.f;
		static inline float s_viewport_height = 1080.f;
	};

	class c_daisy_resettable_object
	{
	public:
		virtual bool reset( bool pre_reset = false ) noexcept = 0;
	};

	// Forward declarations
	class c_fontwrapper;
	class c_texatlas;
	class c_renderqueue;
	class c_doublebuffer_queue;

	// Default shaders as string literals
	namespace shaders
	{
		static const char* vertex_shader_src = R"(
cbuffer ViewportBuffer : register(b0)
{
    float2 ViewportSize;
    float2 _padding;
};

struct VS_INPUT
{
    float4 pos : POSITION;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    
    // Convert from pixel coordinates to clip space [-1, 1]
    // D3D9 used pre-transformed coordinates (RHW), we need to transform here
    output.pos.x = (input.pos.x / ViewportSize.x) * 2.0f - 1.0f;
    output.pos.y = 1.0f - (input.pos.y / ViewportSize.y) * 2.0f;
    output.pos.z = input.pos.z;
    output.pos.w = 1.0f;
    
    output.col = input.col;
    output.uv = input.uv;
    
    return output;
}
)";

		static const char* pixel_shader_src = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 texColor = tex.Sample(samp, input.uv);
    
    // Use texture alpha as a mask for the vertex color
    // This prevents anti-aliased text from appearing gray
    float4 result = input.col;
    result.a *= texColor.a;
    
    return result;
}
)";
	}

	// Helper function to compile shaders
	inline bool compile_shader( const char* source, const char* entry_point, const char* target, ID3DBlob** blob )
	{
		ID3DBlob* error_blob = nullptr;
		HRESULT hr = D3DCompile( source, strlen( source ), nullptr, nullptr, nullptr,
			entry_point, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, blob, &error_blob );

		if ( FAILED( hr ) )
		{
			if ( error_blob )
			{
				OutputDebugStringA( ( char* ) error_blob->GetBufferPointer( ) );
				error_blob->Release( );
			}
			return false;
		}

		if ( error_blob )
			error_blob->Release( );

		return true;
	}

	// Font wrapper class
	class c_fontwrapper : public c_daisy_resettable_object
	{
	private:
		stl::unordered_map<wchar_t, uv_t> m_coords;
		stl::string_view m_family;
		ID3D11Texture2D* m_texture = nullptr;
		ID3D11ShaderResourceView* m_texture_srv = nullptr;
		float m_scale;
		uint32_t m_width, m_height, m_spacing, m_size, m_quality;
		uint8_t m_flags;

	private:
		bool create_ex( ) noexcept
		{
			if ( !daisy_t::s_device )
				return false;

			HDC gdi_ctx = nullptr;
			HGDIOBJ gdi_font = nullptr, prev_gdi_font = nullptr, prev_bitmap = nullptr;
			HBITMAP bitmap = nullptr;

			// Release old texture if exists
			if ( m_texture_srv )
			{
				m_texture_srv->Release( );
				m_texture_srv = nullptr;
			}
			if ( m_texture )
			{
				m_texture->Release( );
				m_texture = nullptr;
			}

			gdi_ctx = CreateCompatibleDC( nullptr );
			SetMapMode( gdi_ctx, MM_TEXT );

			this->create_gdi_font( gdi_ctx, &gdi_font );
			prev_gdi_font = SelectObject( gdi_ctx, gdi_font );

			this->m_width = this->m_height = 128;

			while ( this->paint_or_measure_alphabet( gdi_ctx, true ) == 2 )
			{
				this->m_width *= 2;
				this->m_height *= 2;
			}

			// D3D11 doesn't have device caps in the same way, use reasonable max
			const uint32_t max_texture_size = 16384;

			if ( this->m_width > max_texture_size )
			{
				this->m_scale = static_cast< float >( max_texture_size ) / this->m_width;
				this->m_width = this->m_height = max_texture_size;

				bool first_iteration = true;

				do
				{
					if ( !first_iteration )
						this->m_scale *= 0.9f;

					DeleteObject( SelectObject( gdi_ctx, prev_gdi_font ) );
					this->create_gdi_font( gdi_ctx, &gdi_font );
					prev_gdi_font = SelectObject( gdi_ctx, gdi_font );
					first_iteration = false;
				} while ( this->paint_or_measure_alphabet( gdi_ctx, true ) == 2 );
			}

			// Create D3D11 texture
			D3D11_TEXTURE2D_DESC tex_desc = {};
			tex_desc.Width = this->m_width;
			tex_desc.Height = this->m_height;
			tex_desc.MipLevels = 1;
			tex_desc.ArraySize = 1;
			tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // Use full 8-bit per channel for better quality
			tex_desc.SampleDesc.Count = 1;
			tex_desc.Usage = D3D11_USAGE_DYNAMIC;
			tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			HRESULT hr = daisy_t::s_device->CreateTexture2D( &tex_desc, nullptr, &this->m_texture );
			if ( FAILED( hr ) || !this->m_texture )
				return false;

			// Create shader resource view
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = tex_desc.Format;
			srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MipLevels = 1;

			hr = daisy_t::s_device->CreateShaderResourceView( this->m_texture, &srv_desc, &this->m_texture_srv );
			if ( FAILED( hr ) )
				return false;

			DWORD* bitmap_bits = nullptr;

			BITMAPINFO bitmap_ctx {};
			bitmap_ctx.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
			bitmap_ctx.bmiHeader.biWidth = this->m_width;
			bitmap_ctx.bmiHeader.biHeight = -static_cast< int32_t >( this->m_height );
			bitmap_ctx.bmiHeader.biPlanes = 1;
			bitmap_ctx.bmiHeader.biCompression = BI_RGB;
			bitmap_ctx.bmiHeader.biBitCount = 32;

			bitmap = CreateDIBSection( gdi_ctx, &bitmap_ctx, DIB_RGB_COLORS, reinterpret_cast< void** >( &bitmap_bits ), nullptr, 0 );
			if ( !bitmap || !bitmap_bits )
				return false;

			prev_bitmap = SelectObject( gdi_ctx, bitmap );

			SetTextColor( gdi_ctx, RGB( 255, 255, 255 ) );
			SetBkColor( gdi_ctx, 0x00000000 );
			SetBkMode( gdi_ctx, OPAQUE );
			SetTextAlign( gdi_ctx, TA_TOP );

			if ( this->paint_or_measure_alphabet( gdi_ctx, false ) )
				return false;

			// Map texture and write data
			D3D11_MAPPED_SUBRESOURCE mapped;
			hr = daisy_t::s_context->Map( this->m_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			if ( FAILED( hr ) )
				return false;

			uint8_t* dst_row = static_cast< uint8_t* >( mapped.pData );

			// Write RGBA data
			// GDI renders white text_w on black background
			// We store white RGB (255,255,255) with luminance as alpha
			for ( uint32_t y = 0; y < this->m_height; y++ )
			{
				uint8_t* dst = dst_row;
				for ( uint32_t x = 0; x < this->m_width; x++ )
				{
					// GDI bitmap format is BGRA (B at byte 0, G at 1, R at 2, A at 3)
					DWORD pixel = bitmap_bits[ this->m_width * y + x ];

					// Extract color components
					BYTE b = ( pixel >> 0 ) & 0xFF;
					BYTE g = ( pixel >> 8 ) & 0xFF;
					BYTE r = ( pixel >> 16 ) & 0xFF;

					// Calculate luminance - max of RGB channels for white text_w
					BYTE intensity = max( max( r, g ), b );

					// Write R8G8B8A8: R, G, B, A in memory order
					dst[ 0 ] = 255;      // R - full white
					dst[ 1 ] = 255;      // G - full white  
					dst[ 2 ] = 255;      // B - full white
					dst[ 3 ] = intensity; // A - text_w opacity (0=transparent, 255=solid)

					dst += 4;
				}
				dst_row += mapped.RowPitch;
			}

			daisy_t::s_context->Unmap( this->m_texture, 0 );

			SelectObject( gdi_ctx, prev_bitmap );
			SelectObject( gdi_ctx, prev_gdi_font );
			DeleteObject( bitmap );
			DeleteObject( gdi_font );
			DeleteDC( gdi_ctx );

			return true;
		}

		void create_gdi_font( HDC context, HGDIOBJ* gdi_font ) noexcept
		{
			*gdi_font = CreateFontA( this->m_size, 0, 0, 0, ( this->m_flags & FONT_BOLD ) ? FW_BOLD : FW_NORMAL,
				( this->m_flags & FONT_ITALIC ) ? TRUE : FALSE, FALSE, FALSE, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, this->m_quality, VARIABLE_PITCH, this->m_family.data( ) );
		}

		int paint_or_measure_alphabet( HDC context, bool measure = false ) noexcept
		{
			SIZE size;
			wchar_t chr[ ] = L"x\0\0";

			if ( !GetTextExtentPoint32W( context, chr, 1, &size ) )
				return 1;

			const auto unicode_ranges_size = GetFontUnicodeRanges( context, nullptr );
			if ( !unicode_ranges_size )
				return 1;

			auto glyph_sets_memory = stl::make_unique<uint8_t[ ]>( unicode_ranges_size );
			if ( !glyph_sets_memory )
				return 1;

			auto glyph_sets = reinterpret_cast< GLYPHSET* >( glyph_sets_memory.get( ) );

			if ( !GetFontUnicodeRanges( context, glyph_sets ) )
				return 1;

			this->m_spacing = static_cast< uint32_t >( ceil( size.cy * 0.3f ) );

			uint32_t x = this->m_spacing;
			uint32_t y = 0;

			for ( uint32_t r = 0; r < glyph_sets->cRanges; ++r )
			{
				for ( auto ch = glyph_sets->ranges[ r ].wcLow; ch < ( glyph_sets->ranges[ r ].wcLow + glyph_sets->ranges[ r ].cGlyphs ); ++ch )
				{
					if ( !GetTextExtentPoint32W( context, &ch, 1, &size ) )
						continue;

					if ( x + size.cx + this->m_spacing > this->m_width )
					{
						x = this->m_spacing;
						y += size.cy + 1;
					}

					if ( y + size.cy > this->m_height )
						return 2;

					if ( !measure )
					{
						if ( !ExtTextOutW( context, x + 0, y + 0, ETO_OPAQUE, nullptr, &ch, 1, nullptr ) )
							return 1;

						this->m_coords[ static_cast< uint16_t >( ch ) ][ 0 ] = ( static_cast< float >( x + 0 - this->m_spacing ) ) / this->m_width;
						this->m_coords[ static_cast< uint16_t >( ch ) ][ 1 ] = ( static_cast< float >( y + 0 + 0 ) ) / this->m_height;
						this->m_coords[ static_cast< uint16_t >( ch ) ][ 2 ] = ( static_cast< float >( x + size.cx + this->m_spacing ) ) / this->m_width;
						this->m_coords[ static_cast< uint16_t >( ch ) ][ 3 ] = ( static_cast< float >( y + size.cy + 0 ) ) / this->m_height;
					}

					x += size.cx + ( 2 * this->m_spacing );
				}
			}

			return 0;
		}

	public:
		c_fontwrapper( ) noexcept
			: m_family( ), m_scale( 0.f ), m_width( 0 ), m_height( 0 ), m_spacing( 0 ), m_size( 0 ), m_quality( NONANTIALIASED_QUALITY ), m_flags( 0 )
		{
		}

		~c_fontwrapper( )
		{
			erase( );
		}

		c_fontwrapper( const c_fontwrapper& ) = delete;
		c_fontwrapper& operator=( const c_fontwrapper& ) = delete;

		[[nodiscard]] bool create( const stl::string_view family, uint32_t height, uint32_t quality, uint8_t flags ) noexcept
		{
			this->m_family = family;
			this->m_size = height;
			this->m_flags = flags;
			this->m_quality = quality;
			this->m_scale = 1.f;
			this->m_spacing = 0;

			return this->create_ex( );
		}

		template <typename t = stl::string_view>
		point_t text_extent( t text ) noexcept
		{
			float row_width = 0.f;
			float row_height = ( this->m_coords[ static_cast< wchar_t > ( 32 ) ][ 3 ] - this->m_coords[ static_cast< wchar_t > ( 32 ) ][ 1 ] ) * this->m_height;
			float width = 0.f;
			float height = row_height;

			for ( const auto c : text )
			{
				if ( c == '\n' )
				{
					row_width = 0.f;
					height += row_height;
				}

				if ( c < ' ' )
					continue;

				float tx1 = this->m_coords[ static_cast< wchar_t >( c ) ][ 0 ];
				float tx2 = this->m_coords[ static_cast< wchar_t >( c ) ][ 2 ];

				row_width += ( tx2 - tx1 ) * this->m_width - 2.f * this->m_spacing + 1.f;

				if ( row_width > width )
					width = row_width;
			}

			return { width, height };
		}

		[[nodiscard]] virtual bool reset( bool pre_reset = false ) noexcept override
		{
			if ( !pre_reset )
				return this->create_ex( );
			else
			{
				if ( m_texture_srv )
				{
					m_texture_srv->Release( );
					m_texture_srv = nullptr;
				}
				if ( m_texture )
				{
					m_texture->Release( );
					m_texture = nullptr;
				}
			}
			return true;
		}

		void erase( ) noexcept
		{
			if ( m_texture_srv )
			{
				m_texture_srv->Release( );
				m_texture_srv = nullptr;
			}
			if ( m_texture )
			{
				m_texture->Release( );
				m_texture = nullptr;
			}

			this->m_coords.clear( );
			this->m_size = this->m_spacing = this->m_flags = 0;
			this->m_scale = 1.f;
			this->m_family = "";
		}

		// Getters
		template <typename t = char>
		const uv_t& coords( t glyph ) const noexcept
		{
			if ( this->m_coords.find( glyph ) != this->m_coords.end( ) )
				return this->m_coords.at( glyph );

			static uv_t null_uv { 0.f, 0.f, 0.f, 0.f };
			return null_uv;
		}

		uint32_t spacing( ) const noexcept
		{
			return this->m_spacing;
		}
		uint32_t width( ) const noexcept
		{
			return this->m_width;
		}
		uint32_t height( ) const noexcept
		{
			return this->m_height;
		}
		float scale( ) const noexcept
		{
			return this->m_scale;
		}
		ID3D11ShaderResourceView* texture_srv( ) const noexcept
		{
			return this->m_texture_srv;
		}
	};

	class c_texatlas : public c_daisy_resettable_object
	{
	private:
		point_t m_cursor, m_dimensions;
		ID3D11Texture2D* m_texture = nullptr;
		ID3D11ShaderResourceView* m_texture_srv = nullptr;
		float m_max_height;

	public:
		stl::unordered_map<uint32_t, uv_t> m_coords;

		c_texatlas( ) noexcept
			: m_cursor( { 0.f, 0.f } ), m_dimensions( { 0.f, 0.f } ), m_max_height( 0.f )
		{
		}

		~c_texatlas( )
		{
			if ( m_texture_srv )
				m_texture_srv->Release( );
			if ( m_texture )
				m_texture->Release( );
		}

		c_texatlas( const c_texatlas& ) = delete;
		c_texatlas& operator=( const c_texatlas& ) = delete;

		[[nodiscard]] bool create( const point_t& dimensions ) noexcept
		{
			if ( !daisy_t::s_device )
				return false;

			this->m_dimensions = dimensions;
			this->m_cursor = point_t { 0.f, 0.f };
			this->m_max_height = 0.f;

			D3D11_TEXTURE2D_DESC tex_desc = {};
			tex_desc.Width = static_cast< UINT >( dimensions.x );
			tex_desc.Height = static_cast< UINT >( dimensions.y );
			tex_desc.MipLevels = 1;
			tex_desc.ArraySize = 1;
			tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			tex_desc.SampleDesc.Count = 1;
			tex_desc.Usage = D3D11_USAGE_DYNAMIC;
			tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			HRESULT hr = daisy_t::s_device->CreateTexture2D( &tex_desc, nullptr, &this->m_texture );
			if ( FAILED( hr ) )
				return false;

			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = tex_desc.Format;
			srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MipLevels = 1;

			hr = daisy_t::s_device->CreateShaderResourceView( this->m_texture, &srv_desc, &this->m_texture_srv );
			if ( FAILED( hr ) )
				return false;

			return true;
		}

		[[nodiscard]] virtual bool reset( bool pre_reset = false ) noexcept override
		{
			if ( !pre_reset )
				return this->create( this->m_dimensions );
			else
			{
				if ( m_texture_srv )
				{
					m_texture_srv->Release( );
					m_texture_srv = nullptr;
				}
				if ( m_texture )
				{
					m_texture->Release( );
					m_texture = nullptr;
				}
			}
			return true;
		}

		bool append( const uint32_t uuid, const point_t& dimensions, uint8_t* tex_data, uint32_t tex_size ) noexcept
		{
			if ( !tex_data || !tex_size )
				return false;

			if ( this->m_cursor.x + dimensions.x > this->m_dimensions.x )
			{
				this->m_cursor.y += this->m_max_height;
				this->m_cursor.x = this->m_max_height = 0.f;
			}

			if ( this->m_cursor.y + dimensions.y > this->m_dimensions.y )
				return false;

			if ( this->m_max_height < dimensions.y )
				this->m_max_height = dimensions.y;

			D3D11_MAPPED_SUBRESOURCE mapped;
			HRESULT hr = daisy_t::s_context->Map( this->m_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			if ( FAILED( hr ) )
				return false;

			for ( int y = 0; y < dimensions.y; ++y )
			{
				for ( int x = 0; x < dimensions.x; ++x )
				{
					const uint8_t* source_pixel = tex_data + static_cast< uint32_t >( dimensions.x ) * 4 * y + x * 4;

					if ( static_cast< uint32_t >( dimensions.x ) * 4 * y + x * 4 > tex_size )
						return false;

					uint8_t* destination_pixel = static_cast< uint8_t* >( mapped.pData ) +
						mapped.RowPitch * ( static_cast< uint32_t >( this->m_cursor.y ) + y ) +
						( static_cast< uint32_t >( this->m_cursor.x ) + x ) * 4;

					// RGBA format
					destination_pixel[ 0 ] = source_pixel[ 0 ];
					destination_pixel[ 1 ] = source_pixel[ 1 ];
					destination_pixel[ 2 ] = source_pixel[ 2 ];
					destination_pixel[ 3 ] = source_pixel[ 3 ];
				}
			}

			daisy_t::s_context->Unmap( this->m_texture, 0 );

			auto start_uv = point_t { this->m_cursor.x / this->m_dimensions.x, this->m_cursor.y / this->m_dimensions.y };
			auto end_uv = point_t { start_uv.x + dimensions.x / this->m_dimensions.x, start_uv.y + dimensions.y / this->m_dimensions.y };
			this->m_coords[ uuid ] = uv_t { start_uv.x, start_uv.y, end_uv.x, end_uv.y };

			this->m_cursor.x += dimensions.x;

			return true;
		}

		const uv_t& coords( uint32_t uuid ) const noexcept
		{
			if ( this->m_coords.find( uuid ) != this->m_coords.end( ) )
				return this->m_coords.at( uuid );

			static uv_t null_uv { 0.f, 0.f, 0.f, 0.f };
			return null_uv;
		}

		ID3D11ShaderResourceView* texture_srv( ) const noexcept
		{
			return this->m_texture_srv;
		}
	};

	// Render queue class - continues in next part due to length
	class c_renderqueue : public c_daisy_resettable_object
	{
	private:
		ID3D11Buffer* m_vertex_buffer = nullptr;
		ID3D11Buffer* m_index_buffer = nullptr;
		ID3D11Buffer* m_constant_buffer = nullptr;

		renderbuffer_t m_vtxs, m_idxs;
		stl::vector<daisy_drawcall_t> m_drawcalls;

		bool m_update;
		bool m_realloc_vtx, m_realloc_idx;

	private:
		void ensure_buffers_capacity( const uint32_t vertices_to_add, const uint32_t indices_to_add ) noexcept
		{
			if ( this->m_vtxs.m_size + vertices_to_add > this->m_vtxs.m_capacity )
			{
				while ( this->m_vtxs.m_size + vertices_to_add > this->m_vtxs.m_capacity )
					this->m_vtxs.m_capacity = this->m_vtxs.m_capacity * 2;

				auto new_vtx = stl::make_unique<uint8_t[ ]>( this->m_vtxs.m_capacity * sizeof( daisy_vtx_t ) );

				if ( new_vtx )
				{
					memcpy( new_vtx.get( ), this->m_vtxs.m_data.get( ), this->m_vtxs.m_size * sizeof( daisy_vtx_t ) );
					this->m_realloc_vtx = true;
					this->m_vtxs.m_data.swap( new_vtx );
				}
			}

			if ( this->m_idxs.m_size + indices_to_add > this->m_idxs.m_capacity )
			{
				while ( this->m_idxs.m_size + indices_to_add > this->m_idxs.m_capacity )
					this->m_idxs.m_capacity = this->m_idxs.m_capacity * 2;

				auto new_idx = stl::make_unique<uint8_t[ ]>( this->m_idxs.m_capacity * sizeof( uint16_t ) );

				if ( new_idx )
				{
					memcpy( new_idx.get( ), this->m_idxs.m_data.get( ), this->m_idxs.m_size * sizeof( uint16_t ) );
					this->m_realloc_idx = true;
					this->m_idxs.m_data.swap( new_idx );
				}
			}
		}

		uint32_t begin_batch( ID3D11ShaderResourceView* texture_srv = nullptr ) const noexcept
		{
			uint32_t additional = 0;

			if ( !this->m_drawcalls.empty( ) )
			{
				auto& last_call = this->m_drawcalls.back( );
				if ( last_call.m_kind == daisy_call_kind::CALL_TRI && last_call.m_tri.m_texture_srv == texture_srv )
				{
					additional = last_call.m_tri.m_vertices;
				}
			}

			return additional;
		}

		void end_batch( uint32_t additional_indices, uint32_t vertices, uint32_t indices, uint32_t primitives, ID3D11ShaderResourceView* texture_srv = nullptr )
		{
			if ( !additional_indices )
			{
				daisy_drawcall_t d {};
				d.m_kind = daisy_call_kind::CALL_TRI;
				d.m_tri.m_indices = indices;
				d.m_tri.m_vertices = vertices;
				d.m_tri.m_texture_srv = texture_srv;
				d.m_tri.m_primitives = primitives;

				this->m_drawcalls.push_back( stl::move( d ) );
			}
			else
			{
				auto& last_call = this->m_drawcalls.back( );

				last_call.m_tri.m_vertices += vertices;
				last_call.m_tri.m_indices += indices;
				last_call.m_tri.m_primitives += primitives;
			}

			this->m_update = true;
		}

	public:
		c_renderqueue( ) noexcept
			: m_update( true ), m_realloc_vtx( false ), m_realloc_idx( false )
		{
		}

		~c_renderqueue( )
		{
			if ( m_vertex_buffer )
				m_vertex_buffer->Release( );
			if ( m_index_buffer )
				m_index_buffer->Release( );
			if ( m_constant_buffer )
				m_constant_buffer->Release( );
		}

		c_renderqueue( const c_renderqueue& ) = delete;
		c_renderqueue& operator=( const c_renderqueue& ) = delete;

		[[nodiscard]] bool create( const uint32_t max_verts = 32767, const uint32_t max_indices = 65535 ) noexcept
		{
			if ( !daisy_t::s_device )
				return false;

			// Create vertex buffer
			if ( !this->m_vertex_buffer )
			{
				D3D11_BUFFER_DESC desc = {};
				desc.ByteWidth = sizeof( daisy_vtx_t ) * max_verts;
				desc.Usage = D3D11_USAGE_DYNAMIC;
				desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				if ( FAILED( daisy_t::s_device->CreateBuffer( &desc, nullptr, &this->m_vertex_buffer ) ) )
					return false;
			}

			// Create index buffer
			if ( !this->m_index_buffer )
			{
				D3D11_BUFFER_DESC desc = {};
				desc.ByteWidth = sizeof( uint16_t ) * max_indices;
				desc.Usage = D3D11_USAGE_DYNAMIC;
				desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				if ( FAILED( daisy_t::s_device->CreateBuffer( &desc, nullptr, &this->m_index_buffer ) ) )
					return false;
			}

			// Create constant buffer for viewport
			if ( !this->m_constant_buffer )
			{
				D3D11_BUFFER_DESC desc = {};
				desc.ByteWidth = sizeof( float ) * 4; // ViewportSize + padding
				desc.Usage = D3D11_USAGE_DYNAMIC;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				if ( FAILED( daisy_t::s_device->CreateBuffer( &desc, nullptr, &this->m_constant_buffer ) ) )
					return false;
			}

			// Create local buffers
			if ( !this->m_vtxs.m_data.get( ) )
			{
				this->m_vtxs.m_data = stl::make_unique<uint8_t[ ]>( sizeof( daisy_vtx_t ) * max_verts );
				this->m_vtxs.m_capacity = max_verts;
				this->m_vtxs.m_size = 0;
			}

			if ( !this->m_idxs.m_data )
			{
				this->m_idxs.m_data = stl::make_unique<uint8_t[ ]>( sizeof( uint16_t ) * max_indices );
				this->m_idxs.m_capacity = max_indices;
				this->m_idxs.m_size = 0;
			}

			if ( !this->m_vtxs.m_data || !this->m_idxs.m_data )
				return false;

			return true;
		}

		void clear( ) noexcept
		{
			this->m_vtxs.m_size = 0;
			this->m_idxs.m_size = 0;

			if ( !this->m_drawcalls.empty( ) )
				this->m_drawcalls.clear( );
		}

		[[nodiscard]] virtual bool reset( bool pre_reset = false ) noexcept override
		{
			if ( !pre_reset )
				return this->create( this->m_vtxs.m_capacity, this->m_idxs.m_capacity );
			else
			{
				if ( this->m_vertex_buffer )
				{
					this->m_vertex_buffer->Release( );
					this->m_vertex_buffer = nullptr;
				}

				if ( this->m_index_buffer )
				{
					this->m_index_buffer->Release( );
					this->m_index_buffer = nullptr;
				}

				if ( this->m_constant_buffer )
				{
					this->m_constant_buffer->Release( );
					this->m_constant_buffer = nullptr;
				}
			}

			return true;
		}

		void update( ) noexcept
		{
			if ( !daisy_t::s_device || !daisy_t::s_context )
				return;

			// Reallocate if needed
			if ( this->m_realloc_vtx )
			{
				this->m_vertex_buffer->Release( );

				D3D11_BUFFER_DESC desc = {};
				desc.ByteWidth = static_cast< UINT >( this->m_vtxs.m_capacity * sizeof( daisy_vtx_t ) );
				desc.Usage = D3D11_USAGE_DYNAMIC;
				desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				if ( FAILED( daisy_t::s_device->CreateBuffer( &desc, nullptr, &this->m_vertex_buffer ) ) )
					return;

				this->m_realloc_vtx = false;
			}

			if ( this->m_realloc_idx )
			{
				this->m_index_buffer->Release( );

				D3D11_BUFFER_DESC desc = {};
				desc.ByteWidth = static_cast< UINT >( this->m_idxs.m_capacity * sizeof( uint16_t ) );
				desc.Usage = D3D11_USAGE_DYNAMIC;
				desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				if ( FAILED( daisy_t::s_device->CreateBuffer( &desc, nullptr, &this->m_index_buffer ) ) )
					return;

				this->m_realloc_idx = false;
			}

			// Map and copy vertex data
			D3D11_MAPPED_SUBRESOURCE mapped_vtx;
			if ( SUCCEEDED( daisy_t::s_context->Map( this->m_vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_vtx ) ) )
			{
				memcpy( mapped_vtx.pData, this->m_vtxs.m_data.get( ), sizeof( daisy_vtx_t ) * this->m_vtxs.m_size );
				daisy_t::s_context->Unmap( this->m_vertex_buffer, 0 );
			}

			// Map and copy index data
			D3D11_MAPPED_SUBRESOURCE mapped_idx;
			if ( SUCCEEDED( daisy_t::s_context->Map( this->m_index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_idx ) ) )
			{
				memcpy( mapped_idx.pData, this->m_idxs.m_data.get( ), sizeof( uint16_t ) * this->m_idxs.m_size );
				daisy_t::s_context->Unmap( this->m_index_buffer, 0 );
			}

			// Update constant buffer with viewport size
			D3D11_MAPPED_SUBRESOURCE mapped_cb;
			if ( SUCCEEDED( daisy_t::s_context->Map( this->m_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cb ) ) )
			{
				float* cb_data = ( float* ) mapped_cb.pData;
				cb_data[ 0 ] = daisy_t::s_viewport_width;
				cb_data[ 1 ] = daisy_t::s_viewport_height;
				cb_data[ 2 ] = 0.0f; // padding
				cb_data[ 3 ] = 0.0f; // padding
				daisy_t::s_context->Unmap( this->m_constant_buffer, 0 );
			}

			this->m_update = false;
		}

		void flush( ) noexcept
		{
			if ( this->m_drawcalls.empty( ) )
				return;

			if ( this->m_update )
				this->update( );

			// Set up pipeline state
			UINT stride = sizeof( daisy_vtx_t );
			UINT offset = 0;
			daisy_t::s_context->IASetVertexBuffers( 0, 1, &this->m_vertex_buffer, &stride, &offset );
			daisy_t::s_context->IASetIndexBuffer( this->m_index_buffer, DXGI_FORMAT_R16_UINT, 0 );
			daisy_t::s_context->IASetInputLayout( daisy_t::s_input_layout );
			daisy_t::s_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

			daisy_t::s_context->VSSetShader( daisy_t::s_vertex_shader, nullptr, 0 );
			daisy_t::s_context->PSSetShader( daisy_t::s_pixel_shader, nullptr, 0 );
			daisy_t::s_context->VSSetConstantBuffers( 0, 1, &this->m_constant_buffer );

			daisy_t::s_context->PSSetSamplers( 0, 1, &daisy_t::s_sampler_state );

			daisy_t::s_context->OMSetBlendState( daisy_t::s_blend_state, nullptr, 0xffffffff );
			daisy_t::s_context->RSSetState( daisy_t::s_rasterizer_state );
			daisy_t::s_context->OMSetDepthStencilState( daisy_t::s_depth_stencil_state, 0 );

			uint32_t vertex_idx { 0 }, index_idx { 0 };

			for ( size_t i = 0u; i < this->m_drawcalls.size( ); i++ )
			{
				const auto& cmd = this->m_drawcalls[ i ];

				switch ( cmd.m_kind )
				{
				case daisy_call_kind::CALL_TRI:
				{
					// Use white texture if no texture is provided
					ID3D11ShaderResourceView* srv = cmd.m_tri.m_texture_srv ? cmd.m_tri.m_texture_srv : daisy_t::s_white_texture_srv;
					daisy_t::s_context->PSSetShaderResources( 0, 1, &srv );
					daisy_t::s_context->DrawIndexed( cmd.m_tri.m_indices, index_idx, vertex_idx );

					vertex_idx += cmd.m_tri.m_vertices;
					index_idx += cmd.m_tri.m_indices;
					break;
				}
				case daisy_call_kind::CALL_SCISSOR:
				{
					D3D11_RECT rect;
					rect.left = static_cast< LONG >( cmd.m_scissor.m_position.x );
					rect.top = static_cast< LONG >( cmd.m_scissor.m_position.y );
					rect.right = static_cast< LONG >( cmd.m_scissor.m_position.x + cmd.m_scissor.m_size.x );
					rect.bottom = static_cast< LONG >( cmd.m_scissor.m_position.y + cmd.m_scissor.m_size.y );
					daisy_t::s_context->RSSetScissorRects( 1, &rect );
				}
				break;
				// Shader calls would be handled here if needed
				}
			}
		}

		void push_scissor( point_t& position, point_t& size ) noexcept
		{
			daisy_drawcall_t d {};
			d.m_kind = daisy_call_kind::CALL_SCISSOR;
			d.m_scissor.m_position = position;
			d.m_scissor.m_size = size;

			this->m_drawcalls.push_back( stl::move( d ) );
		}

		void push_gradient_rectangle( const point_t& position, const point_t& size, const color_t c1, const color_t c2, const color_t c3, const color_t c4,
			ID3D11ShaderResourceView* texture_srv = nullptr, const point_t& uv_mins = { 0.f, 0.f }, const point_t& uv_maxs = { 1.f, 1.f } ) noexcept
		{
			this->ensure_buffers_capacity( 4, 6 );

			uint32_t additional_indices = this->begin_batch( texture_srv );

			auto vtx_counter = 0, idx_counter = 0;

			daisy_vtx_t* vtx = reinterpret_cast< daisy_vtx_t* >( reinterpret_cast< uintptr_t >( this->m_vtxs.m_data.get( ) ) + ( sizeof( daisy_vtx_t ) * this->m_vtxs.m_size ) );
			uint16_t* idx = reinterpret_cast< uint16_t* >( reinterpret_cast< uintptr_t >( this->m_idxs.m_data.get( ) ) + ( sizeof( uint16_t ) * this->m_idxs.m_size ) );

			vtx[ vtx_counter++ ] = daisy_vtx_t { { floorf( position.x ), floorf( position.y ), 0.0f, 1.f }, c1.bgra, { uv_mins.x, uv_mins.y } };
			vtx[ vtx_counter++ ] = daisy_vtx_t { { floorf( position.x + size.x ), floorf( position.y ), 0.0f, 1.f }, c2.bgra, { uv_maxs.x, uv_mins.y } };
			vtx[ vtx_counter++ ] = daisy_vtx_t { { floorf( position.x + size.x ), floorf( position.y + size.y ), 0.0f, 1.f }, c4.bgra, { uv_maxs.x, uv_maxs.y } };
			vtx[ vtx_counter++ ] = daisy_vtx_t { { floorf( position.x ), floorf( position.y + size.y ), 0.0f, 1.f }, c3.bgra, { uv_mins.x, uv_maxs.y } };

			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 1 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 3 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 3 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 2 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 1 );

			this->m_vtxs.m_size += 4;
			this->m_idxs.m_size += 6;

			this->end_batch( additional_indices, 4, 6, 2, texture_srv );
		}

		void push_filled_rectangle( const point_t& position, const point_t& size, const color_t col,
			ID3D11ShaderResourceView* texture_srv = nullptr, const point_t& uv_mins = { 0.f, 0.f }, const point_t& uv_maxs = { 1.f, 1.f } ) noexcept
		{
			this->push_gradient_rectangle( position, size, col, col, col, col, texture_srv, uv_mins, uv_maxs );
		}

		void push_filled_triangle( const point_t& p1, const point_t& p2, const point_t& p3, const color_t c1, const color_t c2, const color_t c3,
			ID3D11ShaderResourceView* texture_srv = nullptr, const point_t& uv1 = { 0.f, 0.f }, const point_t& uv2 = { 0.f, 0.f }, const point_t& uv3 = { 0.f, 0.f } ) noexcept
		{
			this->ensure_buffers_capacity( 3, 3 );

			uint32_t additional_indices = this->begin_batch( texture_srv );

			auto vtx_counter = 0, idx_counter = 0;

			daisy_vtx_t* vtx = reinterpret_cast< daisy_vtx_t* >( reinterpret_cast< uintptr_t >( this->m_vtxs.m_data.get( ) ) + ( sizeof( daisy_vtx_t ) * this->m_vtxs.m_size ) );
			uint16_t* idx = reinterpret_cast< uint16_t* >( reinterpret_cast< uintptr_t >( this->m_idxs.m_data.get( ) ) + ( sizeof( uint16_t ) * this->m_idxs.m_size ) );

			vtx[ vtx_counter++ ] = daisy_vtx_t { { p1.x, p1.y, 0.0f, 1.f }, c1.bgra, { uv1.x, uv1.y } };
			vtx[ vtx_counter++ ] = daisy_vtx_t { { p2.x, p2.y, 0.0f, 1.f }, c2.bgra, { uv2.x, uv2.y } };
			vtx[ vtx_counter++ ] = daisy_vtx_t { { p3.x, p3.y, 0.0f, 1.f }, c3.bgra, { uv3.x, uv3.y } };

			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 1 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 2 );

			this->m_vtxs.m_size += 3;
			this->m_idxs.m_size += 3;

			this->end_batch( additional_indices, 3, 3, 1, texture_srv );
		}

		void push_line( const point_t& p1, const point_t& p2, const color_t& col, const float width = 1.f ) noexcept
		{
			this->ensure_buffers_capacity( 4, 6 );

			uint32_t additional_indices = this->begin_batch( nullptr );

			point_t delta = { p2.x - p1.x, p2.y - p1.y };
			float length = sqrtf( delta.x * delta.x + delta.y * delta.y ) + FLT_EPSILON;

			float scale = width / ( 2.f * length );
			point_t radius = { -scale * delta.y, scale * delta.x };

			auto vtx_counter = 0, idx_counter = 0;

			daisy_vtx_t* vtx = reinterpret_cast< daisy_vtx_t* >( reinterpret_cast< uintptr_t >( this->m_vtxs.m_data.get( ) ) + ( sizeof( daisy_vtx_t ) * this->m_vtxs.m_size ) );
			uint16_t* idx = reinterpret_cast< uint16_t* >( reinterpret_cast< uintptr_t >( this->m_idxs.m_data.get( ) ) + ( sizeof( uint16_t ) * this->m_idxs.m_size ) );

			vtx[ vtx_counter++ ] = daisy_vtx_t { { p1.x - radius.x, p1.y - radius.y, 0.0f, 1.f }, col.bgra, { 0.f, 0.f } };
			vtx[ vtx_counter++ ] = daisy_vtx_t { { p1.x + radius.x, p1.y + radius.y, 0.0f, 1.f }, col.bgra, { 1.f, 0.f } };
			vtx[ vtx_counter++ ] = daisy_vtx_t { { p2.x - radius.x, p2.y - radius.y, 0.0f, 1.f }, col.bgra, { 1.f, 1.f } };
			vtx[ vtx_counter++ ] = daisy_vtx_t { { p2.x + radius.x, p2.y + radius.y, 0.0f, 1.f }, col.bgra, { 0.f, 1.f } };

			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 1 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 2 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 2 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 3 );
			idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 1 );

			this->m_vtxs.m_size += 4;
			this->m_idxs.m_size += 6;

			this->end_batch( additional_indices, 4, 6, 2, nullptr );
		}

		void push_filled_circle( const point_t& center, const float radius, const int segments, const color_t& center_color, const color_t& outer_color )
		{
			this->ensure_buffers_capacity( static_cast< uint32_t >( segments + 1 ), static_cast< uint32_t >( segments * 3 ) );

			constexpr static auto Pi = 3.14159265358979323846f;
			constexpr static auto Pi2 = Pi * Pi;

			uint32_t additional_indices = this->begin_batch( nullptr );

			auto vtx_counter = 0, idx_counter = 0;

			daisy_vtx_t* vtx = reinterpret_cast< daisy_vtx_t* >( reinterpret_cast< uintptr_t >( this->m_vtxs.m_data.get( ) ) + ( sizeof( daisy_vtx_t ) * this->m_vtxs.m_size ) );
			uint16_t* idx = reinterpret_cast< uint16_t* >( reinterpret_cast< uintptr_t >( this->m_idxs.m_data.get( ) ) + ( sizeof( uint16_t ) * this->m_idxs.m_size ) );

			vtx[ vtx_counter++ ] = daisy_vtx_t { { center.x, center.y, 0.0f, 1.f }, center_color.bgra, { 0.f, 0.f } };

			for ( int i = 0; i <= segments; ++i )
			{
				float theta = ( 2.f * Pi2 * i ) / segments;
				float x = center.x + radius * cosf( theta );
				float y = center.y + radius * sinf( theta );

				auto last_iteration = ( i == segments );

				if ( !last_iteration )
					vtx[ vtx_counter++ ] = daisy_vtx_t { { x, y, 0.0f, 1.f }, outer_color.bgra, { 0.f, 0.f } };

				if ( i )
				{
					idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices );

					if ( last_iteration )
					{
						idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + i );
						idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + 1 );
					}
					else
					{
						idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + i );
						idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + i + 1 );
					}
				}
			}

			this->m_vtxs.m_size += vtx_counter;
			this->m_idxs.m_size += idx_counter;

			this->end_batch( additional_indices, vtx_counter, idx_counter, segments, nullptr );
		}

		template <typename t = stl::string_view>
		void push_text( c_fontwrapper& font, const point_t& position, const t text, const color_t& color, uint16_t alignment = TEXT_ALIGN_DEFAULT ) noexcept
		{
			this->ensure_buffers_capacity( static_cast< uint32_t >( text.size( ) * 4 ), static_cast< uint32_t >( text.size( ) * 6 ) );

			uint32_t additional_indices = this->begin_batch( font.texture_srv( ) );
			uint32_t cont_vertices = 0, cont_indices = 0, cont_primitives = 0;

			point_t corrected_position { position };

			if ( alignment != TEXT_ALIGN_DEFAULT )
			{
				const auto size = font.text_extent( text );

				if ( alignment & TEXT_ALIGNX_CENTER )
					corrected_position.x -= floorf( 0.5f * size.x );
				else if ( alignment & TEXT_ALIGNX_RIGHT )
					corrected_position.x -= floorf( size.x );

				if ( alignment & TEXT_ALIGNY_CENTER )
					corrected_position.y -= floorf( 0.5f * size.y );
				else if ( alignment & TEXT_ALIGNY_BOTTOM )
					corrected_position.y -= floorf( size.y );
			}

			corrected_position.x -= font.spacing( );

			float start_x = corrected_position.x;
			auto space_coords = font.coords( 'A' );

			auto vtx_counter = 0, idx_counter = 0;

			daisy_vtx_t* vtx = reinterpret_cast< daisy_vtx_t* >( reinterpret_cast< uintptr_t >( this->m_vtxs.m_data.get( ) ) + ( sizeof( daisy_vtx_t ) * this->m_vtxs.m_size ) );
			uint16_t* idx = reinterpret_cast< uint16_t* >( reinterpret_cast< uintptr_t >( this->m_idxs.m_data.get( ) ) + ( sizeof( uint16_t ) * this->m_idxs.m_size ) );

			for ( const auto& c : text )
			{
				if ( c == '\n' || c == L'\n' )
				{
					corrected_position.x = start_x;
					corrected_position.y += ( space_coords[ 3 ] - space_coords[ 1 ] ) * font.height( );
					continue;
				}

				auto is_space = ( c == ' ' || c == L' ' );
				auto coords = font.coords( c );

				float tx1 = coords[ 0 ];
				float ty1 = coords[ 1 ];
				float tx2 = coords[ 2 ];
				float ty2 = coords[ 3 ];

				float w = ( tx2 - tx1 ) * font.width( ) / font.scale( );
				float h = ( ty2 - ty1 ) * font.height( ) / font.scale( );

				if ( !is_space )
				{
					daisy_vtx_t v[ ] = {
						{ { corrected_position.x, corrected_position.y + h, 0.f, 1.f }, color.bgra, { tx1, ty2 } },
						{ { corrected_position.x, corrected_position.y, 0.f, 1.f }, color.bgra, { tx1, ty1 } },
						{ { corrected_position.x + w, corrected_position.y + h, 0.f, 1.f }, color.bgra, { tx2, ty2 } },
						{ { corrected_position.x + w, corrected_position.y, 0.f, 1.f }, color.bgra, { tx2, ty1 } } };

					vtx[ vtx_counter++ ] = v[ 0 ];
					vtx[ vtx_counter++ ] = v[ 1 ];
					vtx[ vtx_counter++ ] = v[ 2 ];
					vtx[ vtx_counter++ ] = v[ 3 ];

					idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + cont_vertices );
					idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + cont_vertices + 1 );
					idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + cont_vertices + 2 );
					idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + cont_vertices + 3 );
					idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + cont_vertices + 2 );
					idx[ idx_counter++ ] = static_cast< uint16_t >( additional_indices + cont_vertices + 1 );

					this->m_vtxs.m_size += 4;
					this->m_idxs.m_size += 6;

					cont_vertices += 4;
					cont_indices += 6;
					cont_primitives += 2;
				}

				corrected_position.x += w - ( 2.f * font.spacing( ) ) + 1;
			}

			this->end_batch( additional_indices, cont_vertices, cont_indices, cont_primitives, font.texture_srv( ) );
		}
	};

	class c_doublebuffer_queue : public c_daisy_resettable_object
	{
	private:
		c_renderqueue m_front_queue, m_back_queue;
		stl::atomic<bool> m_swap_drawlists;

	public:
		[[nodiscard]] bool create( const uint32_t max_verts = 32767, const uint32_t max_indices = 65535 ) noexcept
		{
			if ( !this->m_front_queue.create( max_verts, max_indices ) )
				return false;

			if ( !this->m_back_queue.create( max_verts, max_indices ) )
				return false;

			return true;
		}

		[[nodiscard]] virtual bool reset( bool pre_reset = false ) noexcept override
		{
			return this->m_front_queue.reset( pre_reset ) && this->m_back_queue.reset( pre_reset );
		}

		void swap( ) noexcept
		{
			this->m_swap_drawlists = !this->m_swap_drawlists;
		}

		c_renderqueue* queue( ) noexcept
		{
			return ( !this->m_swap_drawlists ? &this->m_front_queue : &this->m_back_queue );
		}

		void flush( )
		{
			this->m_swap_drawlists ? this->m_front_queue.flush( ) : this->m_back_queue.flush( );
		}
	};

	// Initialization and utility functions
	inline static bool daisy_initialize( ID3D11Device* device, ID3D11DeviceContext* context, float viewport_width, float viewport_height ) noexcept
	{
		daisy_t::s_device = device;
		daisy_t::s_context = context;
		daisy_t::s_viewport_width = viewport_width;
		daisy_t::s_viewport_height = viewport_height;

		if ( device )
			device->AddRef( );
		if ( context )
			context->AddRef( );

		// Create default white texture (1x1 white pixel)
		{
			D3D11_TEXTURE2D_DESC tex_desc = {};
			tex_desc.Width = 1;
			tex_desc.Height = 1;
			tex_desc.MipLevels = 1;
			tex_desc.ArraySize = 1;
			tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			tex_desc.SampleDesc.Count = 1;
			tex_desc.Usage = D3D11_USAGE_DEFAULT;
			tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			uint32_t white_pixel = 0xFFFFFFFF; // RGBA white
			D3D11_SUBRESOURCE_DATA init_data = {};
			init_data.pSysMem = &white_pixel;
			init_data.SysMemPitch = 4;

			HRESULT hr = device->CreateTexture2D( &tex_desc, &init_data, &daisy_t::s_white_texture );
			if ( FAILED( hr ) )
				return false;

			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = tex_desc.Format;
			srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MipLevels = 1;

			hr = device->CreateShaderResourceView( daisy_t::s_white_texture, &srv_desc, &daisy_t::s_white_texture_srv );
			if ( FAILED( hr ) )
				return false;
		}

		// Compile and create shaders
		ID3DBlob* vs_blob = nullptr;
		ID3DBlob* ps_blob = nullptr;

		if ( !compile_shader( shaders::vertex_shader_src, "main", "vs_5_0", &vs_blob ) )
			return false;

		if ( !compile_shader( shaders::pixel_shader_src, "main", "ps_5_0", &ps_blob ) )
		{
			vs_blob->Release( );
			return false;
		}

		HRESULT hr = device->CreateVertexShader( vs_blob->GetBufferPointer( ), vs_blob->GetBufferSize( ), nullptr, &daisy_t::s_vertex_shader );
		if ( FAILED( hr ) )
		{
			vs_blob->Release( );
			ps_blob->Release( );
			return false;
		}

		hr = device->CreatePixelShader( ps_blob->GetBufferPointer( ), ps_blob->GetBufferSize( ), nullptr, &daisy_t::s_pixel_shader );
		if ( FAILED( hr ) )
		{
			vs_blob->Release( );
			ps_blob->Release( );
			return false;
		}

		// Create input layout
		D3D11_INPUT_ELEMENT_DESC layout[ ] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		hr = device->CreateInputLayout( layout, 3, vs_blob->GetBufferPointer( ), vs_blob->GetBufferSize( ), &daisy_t::s_input_layout );

		vs_blob->Release( );
		ps_blob->Release( );

		if ( FAILED( hr ) )
			return false;

		// Create blend state
		D3D11_BLEND_DESC blend_desc = {};
		blend_desc.AlphaToCoverageEnable = FALSE;
		blend_desc.IndependentBlendEnable = FALSE;
		blend_desc.RenderTarget[ 0 ].BlendEnable = TRUE;
		blend_desc.RenderTarget[ 0 ].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blend_desc.RenderTarget[ 0 ].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blend_desc.RenderTarget[ 0 ].BlendOp = D3D11_BLEND_OP_ADD;
		blend_desc.RenderTarget[ 0 ].SrcBlendAlpha = D3D11_BLEND_ONE;
		blend_desc.RenderTarget[ 0 ].DestBlendAlpha = D3D11_BLEND_ZERO;
		blend_desc.RenderTarget[ 0 ].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blend_desc.RenderTarget[ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hr = device->CreateBlendState( &blend_desc, &daisy_t::s_blend_state );
		if ( FAILED( hr ) )
			return false;

		// Create rasterizer state
		D3D11_RASTERIZER_DESC raster_desc = {};
		raster_desc.FillMode = D3D11_FILL_SOLID;
		raster_desc.CullMode = D3D11_CULL_NONE;
		raster_desc.ScissorEnable = TRUE;
		raster_desc.DepthClipEnable = TRUE;

		hr = device->CreateRasterizerState( &raster_desc, &daisy_t::s_rasterizer_state );
		if ( FAILED( hr ) )
			return false;

		// Create depth stencil state
		D3D11_DEPTH_STENCIL_DESC depth_desc = {};
		depth_desc.DepthEnable = FALSE;
		depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		depth_desc.StencilEnable = FALSE;

		hr = device->CreateDepthStencilState( &depth_desc, &daisy_t::s_depth_stencil_state );
		if ( FAILED( hr ) )
			return false;

		// Create sampler state
		D3D11_SAMPLER_DESC sampler_desc = {};
		sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampler_desc.MinLOD = 0;
		sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

		hr = device->CreateSamplerState( &sampler_desc, &daisy_t::s_sampler_state );
		if ( FAILED( hr ) )
			return false;

		return true;
	}

	inline static void daisy_prepare( ) noexcept
	{
		if ( !daisy_t::s_context )
			return;

		daisy_t::s_context->OMSetBlendState( daisy_t::s_blend_state, nullptr, 0xffffffff );
		daisy_t::s_context->RSSetState( daisy_t::s_rasterizer_state );

		daisy_t::s_context->OMSetDepthStencilState( daisy_t::s_depth_stencil_state, 0 );

		daisy_t::s_context->PSSetSamplers( 0, 1, &daisy_t::s_sampler_state );

		daisy_t::s_context->VSSetShader( daisy_t::s_vertex_shader, nullptr, 0 );
		daisy_t::s_context->PSSetShader( daisy_t::s_pixel_shader, nullptr, 0 );

		daisy_t::s_context->IASetInputLayout( daisy_t::s_input_layout );
		daisy_t::s_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

		D3D11_RECT default_rect {};
		default_rect.left = 0;
		default_rect.top = 0;
		default_rect.right = static_cast< LONG >( daisy_t::s_viewport_width );
		default_rect.bottom = static_cast< LONG >( daisy_t::s_viewport_height );
		daisy_t::s_context->RSSetScissorRects( 1, &default_rect );
	}

	inline static void daisy_shutdown( ) noexcept
	{
		if ( daisy_t::s_vertex_shader )
		{
			daisy_t::s_vertex_shader->Release( );
			daisy_t::s_vertex_shader = nullptr;
		}

		if ( daisy_t::s_pixel_shader )
		{
			daisy_t::s_pixel_shader->Release( );
			daisy_t::s_pixel_shader = nullptr;
		}

		if ( daisy_t::s_input_layout )
		{
			daisy_t::s_input_layout->Release( );
			daisy_t::s_input_layout = nullptr;
		}

		if ( daisy_t::s_blend_state )
		{
			daisy_t::s_blend_state->Release( );
			daisy_t::s_blend_state = nullptr;
		}

		if ( daisy_t::s_rasterizer_state )
		{
			daisy_t::s_rasterizer_state->Release( );
			daisy_t::s_rasterizer_state = nullptr;
		}

		if ( daisy_t::s_depth_stencil_state )
		{
			daisy_t::s_depth_stencil_state->Release( );
			daisy_t::s_depth_stencil_state = nullptr;
		}

		if ( daisy_t::s_sampler_state )
		{
			daisy_t::s_sampler_state->Release( );
			daisy_t::s_sampler_state = nullptr;
		}

		if ( daisy_t::s_white_texture_srv )
		{
			daisy_t::s_white_texture_srv->Release( );
			daisy_t::s_white_texture_srv = nullptr;
		}

		if ( daisy_t::s_white_texture )
		{
			daisy_t::s_white_texture->Release( );
			daisy_t::s_white_texture = nullptr;
		}

		if ( daisy_t::s_context )
		{
			daisy_t::s_context->Release( );
			daisy_t::s_context = nullptr;
		}

		if ( daisy_t::s_device )
		{
			daisy_t::s_device->Release( );
			daisy_t::s_device = nullptr;
		}
	}

	inline static void daisy_set_viewport( float width, float height ) noexcept
	{
		daisy_t::s_viewport_width = width;
		daisy_t::s_viewport_height = height;
	}

} // namespace daisy

#endif // _SSE2_DAISY_DX11_INCLUDE_GUARD
