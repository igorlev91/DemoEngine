#include "Precompiled.hpp"
#include "Graphics/Shader.hpp"
using namespace Graphics;

namespace
{
    // Constant definitions.
    const GLuint InvalidHandle = 0;
    const GLint  InvalidAttribute = -1;
    const GLint  InvalidUniform = -1;

    // Available shader types.
    struct ShaderType
    {
        const char* name;
        const char* define;
        GLenum type;
    };

    const int ShaderTypeCount = 3;
    const ShaderType ShaderTypes[ShaderTypeCount] =
    {
        { "vertex shader",   "VERTEX_SHADER",   GL_VERTEX_SHADER   },
        { "geometry shader", "GEOMETRY_SHADER", GL_GEOMETRY_SHADER },
        { "fragment shader", "FRAGMENT_SHADER", GL_FRAGMENT_SHADER },
    };
}

Shader::Shader() :
    m_handle(InvalidHandle)
{
}

Shader::~Shader()
{
    this->DestroyHandle();
}

void Shader::DestroyHandle()
{
    // Release the program handle.
    if(m_handle != InvalidHandle)
    {
        glDeleteProgram(m_handle);
        m_handle = InvalidHandle;
    }
}

bool Shader::Load(std::string filePath)
{
    LOG() << "Loading shader from \"" << filePath << "\" file..." << LOG_INDENT();

    // Load the shader code from a file.
    std::string shaderCode = Utility::GetTextFileContent(filePath);

    if(shaderCode.empty())
    {
        LOG_ERROR() << "Could not read the file!";
        return false;
    }

    // Call the compile method.
    if(!this->Compile(shaderCode))
    {
        LOG_ERROR() << "Could not compile the shader code!";
        return false;
    }

    // Success!
    LOG_INFO() << "Success!";

    return true;
}

bool Shader::Compile(std::string shaderCode)
{
    LOG() << "Compiling shader code..." << LOG_INDENT();

    // Check if handle has been already created.
    VERIFY(m_handle == InvalidHandle, "Shader instance has been already initialized!");

    // Validate arguments.
    if(shaderCode.empty())
    {
        LOG_ERROR() << "Shader code cannot be empty!";
        return false;
    }

    // Setup a cleanup guard.
    bool initialized = false;

    // Create an array of shader objects for each type that can be linked.
    GLuint shaderObjects[ShaderTypeCount] = { 0 };

    SCOPE_GUARD_BEGIN();
    {
        for(int i = 0; i < ShaderTypeCount; ++i)
        {
            glDeleteShader(shaderObjects[i]);
        }
    }
    SCOPE_GUARD_END();

    // Extract shader version.
    std::string shaderVersion;

    std::size_t versionStart = shaderCode.find("#version ");
    std::size_t versionEnd = shaderCode.find('\n', versionStart);

    if(versionStart != std::string::npos)
    {
        shaderVersion = shaderCode.substr(versionStart, versionEnd - versionStart + 1);
        shaderCode.erase(versionStart, versionEnd + 1);
    }

    // Compile shader objects.
    bool shaderObjectsFound = false;

    for(unsigned int i = 0; i < ShaderTypeCount; ++i)
    {
        const ShaderType& shaderType = ShaderTypes[i];
        GLuint& shaderObject = shaderObjects[i];

        // Compile shader object if found.
        if(shaderCode.find(shaderType.define) != std::string::npos)
        {
            shaderObjectsFound = true;

            // Create a shader object.
            shaderObject = glCreateShader(shaderType.type);

            if(shaderObject == InvalidHandle)
            {
                LOG_ERROR() << "Could not create a shader object!";
                return false;
            }

            // Prepare preprocessor define.
            std::string shaderDefine = "#define ";
            shaderDefine += shaderType.define;
            shaderDefine += "\n";

            // Compile shader object code.
            const char* shaderCodeSegments[] =
            {
                shaderVersion.c_str(),
                shaderDefine.c_str(),
                shaderCode.c_str(),
            };

            glShaderSource(shaderObject,
                Utility::StaticArraySize(shaderCodeSegments),
                (const GLchar**)&shaderCodeSegments, nullptr
            );

            glCompileShader(shaderObject);

            // Check compiling results.
            GLint compileStatus = 0;
            glGetShaderiv(shaderObject, GL_COMPILE_STATUS, &compileStatus);

            if(compileStatus == GL_FALSE)
            {
                LOG_ERROR() << "Could not compile a shader object!";

                GLint errorLength = 0;
                glGetShaderiv(shaderObject, GL_INFO_LOG_LENGTH, &errorLength);

                if(errorLength != 0)
                {
                    std::vector<char> errorText(errorLength);
                    glGetShaderInfoLog(shaderObject, errorLength, &errorLength, &errorText[0]);

                    LOG_ERROR() << "Shader compile errors: \"" << errorText.data() << "\"";
                }

                return false;
            }

            LOG_INFO() << "Compiled a " << shaderType.name << ".";
        }
    }

    // Check if any shader objects were found.
    if(shaderObjectsFound == false)
    {
        LOG_ERROR() << "Could not find any shader objects!";
        return false;
    }

    // Create a shader program.
    SCOPE_GUARD_IF(!initialized, this->DestroyHandle());

    m_handle = glCreateProgram();

    if(m_handle == InvalidHandle)
    {
        LOG_ERROR() << "Could not create a shader program!";
        return false;
    }

    // Attach compiled shader objects.
    for(unsigned int i = 0; i < ShaderTypeCount; ++i)
    {
        GLuint& shaderObject = shaderObjects[i];

        if(shaderObject != InvalidHandle)
        {
            glAttachShader(m_handle, shaderObject);
        }
    }

    // Link attached shader objects.
    glLinkProgram(m_handle);

    // Detach already linked shader objects.
    for(unsigned int i = 0; i < ShaderTypeCount; ++i)
    {
        GLuint& shaderObject = shaderObjects[i];

        if(shaderObject != InvalidHandle)
        {
            glDetachShader(m_handle, shaderObject);
        }
    }

    // Check linking results.
    GLint linkStatus = 0;
    glGetProgramiv(m_handle, GL_LINK_STATUS, &linkStatus);

    if(linkStatus == GL_FALSE)
    {
        LOG_ERROR()  << "Could not link a shader program!";

        GLint errorLength = 0;
        glGetProgramiv(m_handle, GL_INFO_LOG_LENGTH, &errorLength);

        if(errorLength != 0)
        {
            std::vector<char> errorText(errorLength);
            glGetProgramInfoLog(m_handle, errorLength, &errorLength, &errorText[0]);

            LOG_ERROR() << "Shader link errors: \"" << errorText.data() << "\"";
        }

        return false;
    }

    LOG_INFO() << "Linked a shader program.";

    // Success!
    LOG_INFO() << "Success!";

    return initialized = true;
}

GLint Shader::GetAttribute(std::string name) const
{
    ASSERT(m_handle != InvalidHandle, "Shader program handle has not been created!");
    ASSERT(!name.empty(), "Attribute name cannot be empty!");

    GLint location = glGetAttribLocation(m_handle, name.c_str());

    return location;
}

GLint Shader::GetUniform(std::string name) const
{
    ASSERT(m_handle != InvalidHandle, "Shader program handle has not been created!");
    ASSERT(!name.empty(), "Uniform name cannot be empty!");

    GLint location = glGetUniformLocation(m_handle, name.c_str());

    return location;
}

GLuint Shader::GetHandle() const
{
    ASSERT(m_handle != InvalidHandle, "Shader program handle has not been created!");

    return m_handle;
}

bool Shader::IsValid() const
{
    return m_handle != InvalidHandle;
}