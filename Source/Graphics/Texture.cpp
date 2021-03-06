#include "Precompiled.hpp"
#include "Graphics/Texture.hpp"
#include "Graphics/RenderContext.hpp"
using namespace Graphics;

TextureLoadInfo::TextureLoadInfo() :
    filePath(""),
    mipmaps(true)
{
}

TextureCreateInfo::TextureCreateInfo() :
    width(0),
    height(0),
    format(GL_INVALID_ENUM),
    mipmaps(true),
    data(nullptr)
{
}

Texture::Texture() :
    m_renderContext(nullptr),
    m_handle(OpenGL::InvalidHandle),
    m_format(OpenGL::InvalidEnum),
    m_width(0),
    m_height(0)
{
}

Texture::~Texture()
{
    this->DestroyHandle();
}

Texture::Texture(Texture&& other) :
    Texture()
{
    // Call the move assignment.
    *this = std::move(other);
}

Texture& Texture::operator=(Texture&& other)
{
    // Swap class members.
    std::swap(m_renderContext, other.m_renderContext);
    std::swap(m_handle, other.m_handle);
    std::swap(m_format, other.m_format);
    std::swap(m_width, other.m_width);
    std::swap(m_height, other.m_height);

    return *this;
}


void Texture::DestroyHandle()
{
    // Destroy the texture handle.
    if(m_handle != OpenGL::InvalidHandle)
    {
        glDeleteTextures(1, &m_handle);
        OpenGL::CheckErrors();

        m_handle = OpenGL::InvalidHandle;
    }
}

bool Texture::Initialize(RenderContext* renderContext, const TextureLoadInfo& info)
{
    LOG() << "Loading texture from \"" << info.filePath << "\" file..." << LOG_INDENT();

    // Check if handle has been already created.
    VERIFY(m_handle == OpenGL::InvalidHandle, "Texture instance has already been initialized!");

    // Validate arguments.
    if(info.filePath.empty())
    {
        LOG_ERROR() << "Invalid argument - \"info.filePath\" is empty!";
        return false;
    }

    // Open the file stream.
    std::ifstream file(info.filePath, std::ios::binary);

    if(!file.is_open())
    {
        LOG_ERROR() << "File could not be opened!";
        return false;
    }

    // Validate the file header.
    const size_t png_sig_size = 8;
    png_byte png_sig[png_sig_size];

    file.read((char*)png_sig, png_sig_size);

    if(png_sig_cmp(png_sig, 0, png_sig_size) != 0)
    {
        LOG_ERROR() << "File path does not contain a valid PNG file!";
        return false;
    }

    // Create format decoder structures.
    png_structp png_read_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);

    if(png_read_ptr == nullptr)
    {
        LOG_ERROR() << "Could not create PNG read structure!";
        return false;
    }

    png_infop png_info_ptr = png_create_info_struct(png_read_ptr);

    if(png_info_ptr == nullptr)
    {
        LOG_ERROR() << "Could not create PNG info structure!";
        return false;
    }

    SCOPE_GUARD_BEGIN();
    {
        png_destroy_read_struct(&png_read_ptr, &png_info_ptr, nullptr);
    }
    SCOPE_GUARD_END();

    // Declare file read function.
    auto png_read_function = [](png_structp png_ptr, png_bytep data, png_size_t length) -> void
    {
        std::ifstream* stream = (std::ifstream*)png_get_io_ptr(png_ptr);
        stream->read((char*)data, length);
    };

    // Declare image buffers.
    png_bytep* png_row_ptrs = nullptr;
    png_byte* png_data_ptr = nullptr;

    SCOPE_GUARD_BEGIN();
    {
        delete[] png_row_ptrs;
        delete[] png_data_ptr;
    }
    SCOPE_GUARD_END();

    // Setup the error handling routine.
    // Apparently a standard way of handling errors with libpng...
    // Library jumps here if one of its functions encounters an error!!!
    // This is the reason why scope guards and other objects are declared
    // before this call. Be aware of how dangerous it is to do in C++.
    // For e.g. objects created past this line will not have their
    // destructors called if the library jumps back here on an error.
    if(setjmp(png_jmpbuf(png_read_ptr)))
    {
        LOG_ERROR() << "Error(s) occurred while reading the file!";
        return false;
    }

    // Setup the file read function.
    png_set_read_fn(png_read_ptr, (png_voidp)&file, png_read_function);

    // Set the amount of already read signature bytes.
    png_set_sig_bytes(png_read_ptr, png_sig_size);

    // Read image info.
    png_read_info(png_read_ptr, png_info_ptr);

    png_uint_32 width = png_get_image_width(png_read_ptr, png_info_ptr);
    png_uint_32 height = png_get_image_height(png_read_ptr, png_info_ptr);
    png_uint_32 depth = png_get_bit_depth(png_read_ptr, png_info_ptr);
    png_uint_32 channels = png_get_channels(png_read_ptr, png_info_ptr);
    png_uint_32 format = png_get_color_type(png_read_ptr, png_info_ptr);

    // Process different format types.
    switch(format)
    {
    case PNG_COLOR_TYPE_GRAY:
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        if(depth < 8)
        {
            // Convert gray scale image to single 8bit channel.
            png_set_expand_gray_1_2_4_to_8(png_read_ptr);
            depth = 8;
        }
        break;

    case PNG_COLOR_TYPE_PALETTE:
        {
            // Convert indexed palette to RGB.
            png_set_palette_to_rgb(png_read_ptr);
            channels = 3;

            // Create alpha channel if palette has transparency.
            if(png_get_valid(png_read_ptr, png_info_ptr, PNG_INFO_tRNS))
            {
                png_set_tRNS_to_alpha(png_read_ptr);
                channels += 1;
            }
        }
        break;

    case PNG_COLOR_TYPE_RGB:
    case PNG_COLOR_TYPE_RGBA:
        break;

    default:
        LOG_ERROR() << "Unsupported image format!";
        return false;
    }

    // Make sure we only get 8bits per channel.
    if(depth == 16)
    {
        png_set_strip_16(png_read_ptr);
    }

    if(depth != 8)
    {
        LOG_ERROR() << "Unsupported image depth size!";
        return false;
    }

    // Allocate image buffers.
    png_row_ptrs = new png_bytep[height];
    png_data_ptr = new png_byte[width * height * channels];

    // Setup an array of row pointers to the actual data buffer.
    png_uint_32 png_stride = width * channels;

    for(png_uint_32 i = 0; i < height; ++i)
    {
        // Reverse the order of rows to flip the image.
        // This is done because OpenGL's texture coordinates are also flipped.
        png_uint_32 png_row_index = height - i - 1;

        // Assemble an array of row pointers.
        png_uint_32 png_offset = i * png_stride;
        png_row_ptrs[png_row_index] = png_data_ptr + png_offset;
    }

    // Read image data.
    png_read_image(png_read_ptr, png_row_ptrs);

    // Determine the texture format.
    GLenum textureFormat = GL_NONE;

    switch(channels)
    {
    case 1:
        textureFormat = GL_R;
        break;

    case 2:
        textureFormat = GL_RG;
        break;

    case 3:
        textureFormat = GL_RGB;
        break;

    case 4:
        textureFormat = GL_RGBA;
        break;

    default:
        LOG_ERROR() << "Unsupported number of channels!";
        return false;
    }

    // Call the initialization method.
    TextureCreateInfo textureInfo;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.format = textureFormat;
    textureInfo.mipmaps = info.mipmaps;
    textureInfo.data = png_data_ptr;

    if(!this->Initialize(renderContext, textureInfo))
    {
        LOG_ERROR() << "Could not initialize texture!";
        return false;
    }

    // Success!
    return true;
}

bool Texture::Initialize(RenderContext* renderContext, const TextureCreateInfo& info)
{
    LOG() << "Creating texture..." << LOG_INDENT();

    // Check if handle has been already created.
    VERIFY(m_handle == OpenGL::InvalidHandle, "Texture instance has been already initialized!");

    // Setup a cleanup guard.
    bool initialized = false;

    // Validate arguments.
    if(info.width <= 0)
    {
        LOG_ERROR() << "Invalid argument - \"width\" is invalid.";
        return false;
    }

    if(info.height <= 0)
    {
        LOG_ERROR() << "Invalid argument - \"height\" is invalid.";
        return false;
    }

    if(info.format == GL_INVALID_ENUM)
    {
        LOG_ERROR() << "Invalid argument - \"format\" is invalid.";
        return false;
    }

    // Create a texture handle.
    SCOPE_GUARD_IF(!initialized, this->DestroyHandle());

    glGenTextures(1, &m_handle);
    OpenGL::CheckErrors();

    if(m_handle == OpenGL::InvalidHandle)
    {
        LOG_ERROR() << "Texture could not be created!";
        return false;
    }

    // Bind the texture.
    glBindTexture(GL_TEXTURE_2D, m_handle);
    OpenGL::CheckErrors();

    SCOPE_GUARD(glBindTexture(GL_TEXTURE_2D,
        renderContext->GetState().GetTextureBinding(GL_TEXTURE_2D)));

    // Set packing alignment for provided data.
    if(info.format == GL_R || info.format == GL_RED)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        OpenGL::CheckErrors();
    }

    SCOPE_GUARD(glPixelStorei(GL_UNPACK_ALIGNMENT,
        renderContext->GetState().GetPixelStore(GL_UNPACK_ALIGNMENT)));

    // Allocated a texture surface on the hardware.
    glTexImage2D(GL_TEXTURE_2D, 0, info.format, info.width, info.height,
        0, info.format, GL_UNSIGNED_BYTE, info.data);

    // Generate texture mipmap.
    if(info.mipmaps)
    {
        glGenerateMipmap(GL_TEXTURE_2D);
        OpenGL::CheckErrors();
    }

    // Save texture parameters.
    m_format = info.format;
    m_width = info.width;
    m_height = info.height;

    // Save render context reference.
    m_renderContext = renderContext;

    // Success!
    return initialized = true;
}

void Texture::Update(const void* data)
{
    VERIFY(m_handle != OpenGL::InvalidHandle, "Texture handle has not been created!");
    VERIFY(data != nullptr, "Invalid argument - \"data\" is null!");

    // Upload new texture data.
    glBindTexture(GL_TEXTURE_2D, m_handle);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, m_format, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, m_renderContext->GetState().GetTextureBinding(GL_TEXTURE_2D));
    OpenGL::CheckErrors();
}

GLuint Texture::GetHandle() const
{
    ASSERT(m_handle != OpenGL::InvalidHandle, "Texture handle has not been created!");

    return m_handle;
}

int Texture::GetWidth() const
{
    ASSERT(m_handle != OpenGL::InvalidHandle, "Texture handle has not been created!");

    return m_width;
}

int Texture::GetHeight() const
{
    ASSERT(m_handle != OpenGL::InvalidHandle, "Texture handle has not been created!");

    return m_height;
}

bool Texture::IsValid() const
{
    return m_handle != OpenGL::InvalidHandle;
}
