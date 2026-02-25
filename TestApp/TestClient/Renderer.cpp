#include "pch.h"
#include "Renderer.h"
#include <tiny_gltf.h>

namespace 
{
    static void PrintIfNotEmpty(const char* tag, const std::string& s)
    {
        if (!s.empty()) std::printf("%s: %s\n", tag, s.c_str());
    }

    static int ComponentByteSize(int componentType)
    {
        switch (componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_BYTE:           return 1;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:          return 2;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
        case TINYGLTF_COMPONENT_TYPE_INT:            return 4;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   return 4;
        case TINYGLTF_COMPONENT_TYPE_FLOAT:          return 4;
        case TINYGLTF_COMPONENT_TYPE_DOUBLE:         return 8;
        default: return 0;
        }
    }

    static int TypeNumComponents(int type)
    {
        switch (type)
        {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2:   return 2;
        case TINYGLTF_TYPE_VEC3:   return 3;
        case TINYGLTF_TYPE_VEC4:   return 4;
        case TINYGLTF_TYPE_MAT4:   return 16;
        default: return 0;
        }
    }

    static const uint8_t* GetAccessorDataPtr(const tinygltf::Model& model, const tinygltf::Accessor& acc)
    {
        const auto& view = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[view.buffer];
        return buf.data.data() + view.byteOffset + acc.byteOffset;
    }

    static size_t GetAccessorStride(const tinygltf::Model& model, const tinygltf::Accessor& acc)
    {
        const auto& view = model.bufferViews[acc.bufferView];
        if (view.byteStride > 0) return (size_t)view.byteStride;
        return static_cast<size_t>(ComponentByteSize(acc.componentType) * TypeNumComponents(acc.type));
    }

    static void ReadIndicesU32(const tinygltf::Model& model, const tinygltf::Accessor& acc, std::vector<uint32_t>& out)
    {
        if (acc.type != TINYGLTF_TYPE_SCALAR)
            throw std::runtime_error("Indices accessor must be SCALAR");

        const uint8_t* base = GetAccessorDataPtr(model, acc);
        const size_t stride = GetAccessorStride(model, acc);

        out.resize(acc.count);

        for (size_t i = 0; i < acc.count; ++i)
        {
            const uint8_t* p = base + i * stride;
            switch (acc.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                out[i] = *(const uint8_t*)p;
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                out[i] = *reinterpret_cast<const uint16_t*>(p);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                out[i] = *reinterpret_cast<const uint32_t*>(p);
                break;
            default:
                throw std::runtime_error("Unsupported index componentType");
            }
        }
    }

    static void ReadVec3Float(const tinygltf::Model& model, const tinygltf::Accessor& acc, std::vector<glm::vec3>& out)
    {
        if (acc.type != TINYGLTF_TYPE_VEC3 || acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
            throw std::runtime_error("Expected VEC3 float accessor");

        const uint8_t* base = GetAccessorDataPtr(model, acc);
        const size_t stride = GetAccessorStride(model, acc);

        out.resize(acc.count);

        for (size_t i = 0; i < acc.count; ++i)
        {
            const float* p = reinterpret_cast<const float*>(base + i * stride);
            out[i] = glm::vec3(p[0], p[1], p[2]);
        }
    }
}





void Renderer::Init(uint32 numWindows /*=1*/)
{
    m_vertexShaderSource =
        R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;

            uniform mat4 uMVP;
            uniform mat4 uModel;

            out vec3 FragPos;
            out vec3 Normal;

            void main()
            {
                FragPos = vec3(uModel * vec4(aPos, 1.0));
                Normal = mat3(transpose(inverse(uModel))) * aNormal;
                gl_Position = uMVP * vec4(aPos, 1.0);
            }
        )";

    m_fragmentShaderSource =
        R"(
            #version 330 core
            in vec3 FragPos;
            in vec3 Normal;

            out vec4 FragColor;

            uniform vec4 uColor;
            uniform vec3 uLightDir;
            uniform vec3 uLightColor;
            uniform vec3 uAmbientColor;
            uniform vec3 uViewPos;

            void main()
            {
                // Ambient lighting
                vec3 ambient = uAmbientColor * uColor.rgb;

                // Diffuse lighting
                vec3 norm = normalize(Normal);
                vec3 lightDir = normalize(-uLightDir);
                float diff = max(dot(norm, lightDir), 0.0);
                vec3 diffuse = diff * uLightColor * uColor.rgb;

                // Specular lighting (simple)
                vec3 viewDir = normalize(uViewPos - FragPos);
                vec3 reflectDir = reflect(-lightDir, norm);
                float spec = pow(max(dot(viewDir, reflectDir), 0.0), 16.0);
                vec3 specular = spec * uLightColor * 0.3;

                vec3 result = ambient + diffuse + specular;
                FragColor = vec4(result, uColor.a);
            }
        )";

    if (!glfwInit())
    {
        JAMNET_LOG_WARN_LOC("glfwInit failed");
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwSwapInterval(1);

    m_windowCount = std::clamp<uint32>(numWindows, 1, MAX_WINDOWS);

    // =========================
    // 첫 번째 윈도우 생성
    // =========================
    m_windows[0] = glfwCreateWindow(800, 600, "Client #1", nullptr, nullptr);
    if (!m_windows[0])
    {
        JAMNET_LOG_WARN_LOC("glfwCreateWindow failed for index 0");
        glfwTerminate();
        return;
    }

    glfwSetWindowUserPointer(m_windows[0], this);
    glfwSetFramebufferSizeCallback(m_windows[0], FramebufferSizeCallbackStatic);

    glfwMakeContextCurrent(m_windows[0]);
    m_currentWindowIndex = 0;

    // GLAD 초기화 (첫 번째 컨텍스트에서만)
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        JAMNET_LOG_WARN("[Rendering] Failed to initialize GLAD");
        glfwDestroyWindow(m_windows[0]);
        glfwTerminate();
        return;
    }

    // OpenGL 공통 설정
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f); // Sky blue
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 셰이더 컴파일 및 프로그램 생성
    GLuint vs = CompileShader(GL_VERTEX_SHADER, m_vertexShaderSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, m_fragmentShaderSource);

    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vs);
    glAttachShader(m_shaderProgram, fs);
    glLinkProgram(m_shaderProgram);

    // 링크 검증
    int success;
    glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        char info[512];
        glGetProgramInfoLog(m_shaderProgram, 512, nullptr, info);
        JAMNET_LOG_WARN("[Rendering] Shader Link Error: {}", info);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    m_shaderLocMVP          = glGetUniformLocation(m_shaderProgram, "uMVP");
    m_shaderLocColor        = glGetUniformLocation(m_shaderProgram, "uColor");
    m_shaderLocModel        = glGetUniformLocation(m_shaderProgram, "uModel");
    m_shaderLocLightDir     = glGetUniformLocation(m_shaderProgram, "uLightDir");
    m_shaderLocLightColor   = glGetUniformLocation(m_shaderProgram, "uLightColor");
    m_shaderLocAmbientColor = glGetUniformLocation(m_shaderProgram, "uAmbientColor");
    m_shaderLocViewPos      = glGetUniformLocation(m_shaderProgram, "uViewPos");

    // 기하 데이터의 "버퍼" 초기화 (VBO/EBO – 한 번만)
    InitGrid();
    InitBox();
    InitPlane();
    InitRay();
    // Sphere / Capsule은 lazy init (Draw할 때 InitSphere / InitCapsule 호출)


    // 초기 뷰/프로젝션
    {
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(m_windows[0], &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);

        const float aspect = (fbw > 0 && fbh > 0)
            ? (static_cast<float>(fbw) / static_cast<float>(fbh))
            : (4.0f / 3.0f);

        m_proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        constexpr glm::vec3 eye     = glm::vec3(0.0f, 2.5f, 6.0f);
        constexpr glm::vec3 target  = glm::vec3(0.0f, 0.5f, 0.0f);
        m_view = glm::lookAt(eye, target, m_cameraUp);
    }

    // 첫 번째 윈도우의 VAO 생성
    EnsureGridVAO();
    EnsurePlaneVAO();
    EnsureBoxVAO();
    EnsureRayVAO();
    // Sphere / Capsule은 lazy

    // =========================
    // 나머지 윈도우 생성 (컨텍스트 공유)
    // =========================
    for (int32 i = 1; i < static_cast<int32>(m_windowCount); ++i)
    {
        std::string title = "Client #" + std::to_string(i + 1);

        m_windows[i] = glfwCreateWindow(800, 600, title.c_str(), nullptr, m_windows[0]);
        if (!m_windows[i])
        {
            JAMNET_LOG_WARN_LOC("glfwCreateWindow failed index={}", i);
            m_windowCount = i;
            break;
        }

        glfwSetWindowUserPointer(m_windows[i], this);
        glfwSetFramebufferSizeCallback(m_windows[i], FramebufferSizeCallbackStatic);

        glfwMakeContextCurrent(m_windows[i]);
        m_currentWindowIndex = i;

        glEnable(GL_DEPTH_TEST);
        glClearColor(0.53f, 0.81f, 0.92f, 1.0f); // Sky blue
        glEnable(GL_MULTISAMPLE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glfwSwapInterval(1);


        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(m_windows[i], &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);

        // 이 컨텍스트(윈도우)용 VAO 생성
        EnsureGridVAO();
        EnsurePlaneVAO();
        EnsureBoxVAO();
        EnsureRayVAO();
    }

    // 다시 0번 윈도우를 현재 컨텍스트로
    glfwMakeContextCurrent(m_windows[0]);
    m_currentWindowIndex = 0;

    JAMNET_LOG_INFO("[Rendering] Initialized {} window(s)", m_windowCount);
}

void Renderer::Shutdown()
{
    // 첫 번째 윈도우 컨텍스트에서 리소스 정리
    if (m_windows[0])
    {
        glfwMakeContextCurrent(m_windows[0]);
        m_currentWindowIndex = 0;

        // 윈도우별 VAO 삭제
        for (uint32 i = 0; i < m_windowCount; ++i)
        {
            if (m_gridVAO[i])
            {
                glDeleteVertexArrays(1, &m_gridVAO[i]);
                m_gridVAO[i] = 0;
            }
            if (m_planeVAO[i])
            {
                glDeleteVertexArrays(1, &m_planeVAO[i]);
                m_planeVAO[i] = 0;
            }
            if (m_boxVAO[i])
            {
                glDeleteVertexArrays(1, &m_boxVAO[i]);
                m_boxVAO[i] = 0;
            }
            if (m_sphereVAO[i])
            {
                glDeleteVertexArrays(1, &m_sphereVAO[i]);
                m_sphereVAO[i] = 0;
            }
            if (m_capsuleVAO[i])
            {
                glDeleteVertexArrays(1, &m_capsuleVAO[i]);
                m_capsuleVAO[i] = 0;
            }
            if (m_rayVAO[i])
            {
                glDeleteVertexArrays(1, &m_rayVAO[i]);
                m_rayVAO[i] = 0;
            }
            if (m_crossVAO[i])
            {
                glDeleteVertexArrays(1, &m_crossVAO[i]);
                m_crossVAO[i] = 0;
            }
        }

        // 공유 버퍼 삭제
        if (m_gridInitialized)
        {
            glDeleteBuffers(1, &m_gridVBO);
            m_gridVBO = 0;
            m_gridInitialized = false;
            m_gridVertexCount = 0;
        }

        if (m_planeInitialized)
        {
            glDeleteBuffers(1, &m_planeVBO);
            glDeleteBuffers(1, &m_planeEBO);
            m_planeVBO = 0;
            m_planeEBO = 0;
            m_planeInitialized = false;
        }

        if (m_boxInitialized)
        {
            glDeleteBuffers(1, &m_boxVBO);
            glDeleteBuffers(1, &m_boxEBO);
            m_boxVBO = 0;
            m_boxEBO = 0;
            m_boxInitialized = false;
        }

        if (m_sphereInitialized)
        {
            glDeleteBuffers(1, &m_sphereVBO);
            glDeleteBuffers(1, &m_sphereEBO);
            m_sphereVBO = 0;
            m_sphereEBO = 0;
            m_sphereInitialized = false;
            m_sphereIndexCount = 0;
        }

        if (m_capsuleInitialized)
        {
            glDeleteBuffers(1, &m_capsuleVBO);
            glDeleteBuffers(1, &m_capsuleEBO);
            m_capsuleVBO = 0;
            m_capsuleEBO = 0;
            m_capsuleInitialized = false;
            m_capsuleIndexCount = 0;
        }

        if (m_rayInitialized)
        {
            glDeleteBuffers(1, &m_rayVBO);
            m_rayVBO = 0;
            m_rayInitialized = false;
        }

        if (m_crosshairInitialized)
        {
            glDeleteBuffers(1, &m_crossVBO);
            m_crossVBO = 0;
            m_crosshairInitialized = false;
        }

        if (m_shaderProgram)
        {
            glDeleteProgram(m_shaderProgram);
            m_shaderProgram = 0;
        }

    }

    // 윈도우 파괴
    for (uint32 i = 0; i < m_windowCount; ++i)
    {
        if (m_windows[i])
        {
            glfwDestroyWindow(m_windows[i]);
            m_windows[i] = nullptr;
        }
    }
    m_windowCount = 0;
    m_currentWindowIndex = -1;

    glfwTerminate();
}

bool Renderer::ShouldClose() const
{
    if (m_windowCount == 0)
        return true;

    for (uint32 i = 0; i < m_windowCount; ++i)
    {
        if (m_windows[i] && glfwWindowShouldClose(m_windows[i]))
            return true;
    }
    return false;
}

void Renderer::PreRender(uint32 index)
{
    if (index >= m_windowCount)
        return;

    GLFWwindow* window = m_windows[index];
    if (!window) return;

    glfwMakeContextCurrent(window);
    m_currentWindowIndex = static_cast<int32>(index);

    // Keep viewport/projection in sync per-window.
    // Without this, window #1 can render correctly while other windows use a stale viewport.
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    const float aspect = (fbw > 0 && fbh > 0)
        ? (static_cast<float>(fbw) / static_cast<float>(fbh))
        : (4.0f / 3.0f);
    m_proj = glm::perspective(glm::radians(m_camFovDeg), aspect, m_camNear, m_camFar);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::PostRender(uint32 index)
{
    if (index >= m_windowCount)
        return;

    GLFWwindow* window = m_windows[index];
    if (!window) return;

    glfwMakeContextCurrent(window);
    m_currentWindowIndex = static_cast<int32>(index);


    glfwSwapBuffers(window);

    // Pump events once per frame so resize callbacks fire for all windows.
    // (GLFW processes events globally, not per-window.)
    if (index == 0)
        glfwPollEvents();
}

void Renderer::DrawGrid(const glm::vec4& color)
{
    if (!m_gridInitialized || m_currentWindowIndex < 0)
        return;

    EnsureGridVAO();
    GLuint vao = m_gridVAO[m_currentWindowIndex];
    if (!vao) return;

    glUseProgram(m_shaderProgram);

    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 mvp = m_proj * m_view * model;

    glUniformMatrix4fv(m_shaderLocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(m_shaderLocModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniform4f(m_shaderLocColor, color.r, color.g, color.b, color.a);

    // Lighting uniforms (minimal for grid lines)
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
    glm::vec3 lightColor(1.0f, 0.95f, 0.8f);
    glm::vec3 ambientColor(0.35f, 0.4f, 0.5f);

    glUniform3fv(m_shaderLocLightDir, 1, glm::value_ptr(lightDir));
    glUniform3fv(m_shaderLocLightColor, 1, glm::value_ptr(lightColor));
    glUniform3fv(m_shaderLocAmbientColor, 1, glm::value_ptr(ambientColor));
    glUniform3fv(m_shaderLocViewPos, 1, glm::value_ptr(m_cameraEye));

    glBindVertexArray(vao);
    glDrawArrays(GL_LINES, 0, m_gridVertexCount);
    glBindVertexArray(0);
}

void Renderer::DrawPlane(const glm::vec3& position, const glm::vec2& scale, const glm::vec4& color)
{
    if (!m_planeInitialized || m_currentWindowIndex < 0)
        return;

    EnsurePlaneVAO();
    GLuint vao = m_planeVAO[m_currentWindowIndex];
    if (!vao) return;

    glUseProgram(m_shaderProgram);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, glm::vec3(scale.x, 1.0f, scale.y));

    glm::mat4 mvp = m_proj * m_view * model;

    glUniformMatrix4fv(m_shaderLocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(m_shaderLocModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniform4f(m_shaderLocColor, color.r, color.g, color.b, color.a);

    // Lighting uniforms
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
    glm::vec3 lightColor(1.0f, 0.95f, 0.8f);
    glm::vec3 ambientColor(0.35f, 0.4f, 0.5f);

    glUniform3fv(m_shaderLocLightDir, 1, glm::value_ptr(lightDir));
    glUniform3fv(m_shaderLocLightColor, 1, glm::value_ptr(lightColor));
    glUniform3fv(m_shaderLocAmbientColor, 1, glm::value_ptr(ambientColor));
    glUniform3fv(m_shaderLocViewPos, 1, glm::value_ptr(m_cameraEye));

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::DrawBox(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale, const glm::vec4& color)
{
    if (!m_boxInitialized || m_currentWindowIndex < 0)
        return;

    EnsureBoxVAO();
    GLuint vao = m_boxVAO[m_currentWindowIndex];
    if (!vao) return;

    glUseProgram(m_shaderProgram);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, rotation.x, glm::vec3(1, 0, 0));
    model = glm::rotate(model, rotation.y, glm::vec3(0, 1, 0));
    model = glm::rotate(model, rotation.z, glm::vec3(0, 0, 1));
    model = glm::scale(model, scale);

    glm::mat4 mvp = m_proj * m_view * model;

    glUniformMatrix4fv(m_shaderLocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(m_shaderLocModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniform4f(m_shaderLocColor, color.r, color.g, color.b, color.a);

    // Lighting uniforms
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f)); // Directional light
    glm::vec3 lightColor(1.0f, 0.95f, 0.8f); // Warm sunlight
    glm::vec3 ambientColor(0.35f, 0.4f, 0.5f); // Cool ambient

    glUniform3fv(m_shaderLocLightDir, 1, glm::value_ptr(lightDir));
    glUniform3fv(m_shaderLocLightColor, 1, glm::value_ptr(lightColor));
    glUniform3fv(m_shaderLocAmbientColor, 1, glm::value_ptr(ambientColor));
    glUniform3fv(m_shaderLocViewPos, 1, glm::value_ptr(m_cameraEye));

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::DrawSphere(const glm::vec3& position, float radius, const glm::vec4& color)
{
    if (!m_sphereInitialized)
        InitSphere();

    if (!m_sphereInitialized || m_currentWindowIndex < 0)
        return;

    EnsureSphereVAO();
    GLuint vao = m_sphereVAO[m_currentWindowIndex];
    if (!vao) return;

    glUseProgram(m_shaderProgram);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, glm::vec3(radius));

    glm::mat4 mvp = m_proj * m_view * model;

    glUniformMatrix4fv(m_shaderLocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(m_shaderLocModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniform4f(m_shaderLocColor, color.r, color.g, color.b, color.a);

    // Lighting uniforms
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
    glm::vec3 lightColor(1.0f, 0.95f, 0.8f);
    glm::vec3 ambientColor(0.35f, 0.4f, 0.5f);

    glUniform3fv(m_shaderLocLightDir, 1, glm::value_ptr(lightDir));
    glUniform3fv(m_shaderLocLightColor, 1, glm::value_ptr(lightColor));
    glUniform3fv(m_shaderLocAmbientColor, 1, glm::value_ptr(ambientColor));
    glUniform3fv(m_shaderLocViewPos, 1, glm::value_ptr(m_cameraEye));

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::DrawCapsule(const glm::vec3& position, float radius, float halfHeight, const glm::vec4& color)
{
    if (!m_capsuleInitialized)
        InitCapsule();

    if (!m_capsuleInitialized || m_currentWindowIndex < 0)
        return;

    EnsureCapsuleVAO();
    GLuint vao = m_capsuleVAO[m_currentWindowIndex];
    if (!vao) return;

    glUseProgram(m_shaderProgram);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, glm::vec3(radius, halfHeight, radius));

    glm::mat4 mvp = m_proj * m_view * model;
    glUniformMatrix4fv(m_shaderLocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform4f(m_shaderLocColor, color.r, color.g, color.b, color.a);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, m_capsuleIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::DrawRay(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color)
{
    if (!m_rayInitialized || m_currentWindowIndex < 0)
        return;

    EnsureRayVAO();
    GLuint vao = m_rayVAO[m_currentWindowIndex];
    if (!vao) return;

    // Position + Normal (6 floats per vertex)
    float rayVertices[12] = {
        start.x, start.y, start.z,  0.0f, 1.0f, 0.0f,
        end.x, end.y, end.z,  0.0f, 1.0f, 0.0f
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_rayVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rayVertices), rayVertices);

    glUseProgram(m_shaderProgram);

    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 mvp = m_proj * m_view * model;

    glUniformMatrix4fv(m_shaderLocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(m_shaderLocModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniform4f(m_shaderLocColor, color.r, color.g, color.b, color.a);

    // Lighting uniforms
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
    glm::vec3 lightColor(1.0f, 0.95f, 0.8f);
    glm::vec3 ambientColor(0.35f, 0.4f, 0.5f);

    glUniform3fv(m_shaderLocLightDir, 1, glm::value_ptr(lightDir));
    glUniform3fv(m_shaderLocLightColor, 1, glm::value_ptr(lightColor));
    glUniform3fv(m_shaderLocAmbientColor, 1, glm::value_ptr(ambientColor));
    glUniform3fv(m_shaderLocViewPos, 1, glm::value_ptr(m_cameraEye));

    glLineWidth(2.0f);
    glBindVertexArray(vao);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0);
}

bool Renderer::LoadGLTFScene(const std::string& path)
{
    // 반드시 GL 컨텍스트가 잡힌 상태에서 호출
    if (m_currentWindowIndex < 0 && m_windows[0])
    {
        glfwMakeContextCurrent(m_windows[0]);
        m_currentWindowIndex = 0;
    }

    ClearGLTFScene();
    return BuildItemsFromGLTF(path);
}

void Renderer::ClearGLTFScene()
{
    for (auto& it : m_gltfItems)
        DestroyGpuMesh(it.mesh);
    m_gltfItems.clear();
}

void Renderer::DrawLoadedGLTF(const glm::vec4* colorOverride)
{
    if (m_gltfItems.empty())
        return;

    for (const auto& it : m_gltfItems)
    {
        RenderItem tmp = it;
        if (colorOverride) tmp.color = *colorOverride;
        DrawStaticMesh(tmp);
    }
}

void Renderer::DrawStaticMesh(const RenderItem& item)
{
    glUseProgram(m_shaderProgram);

    glUniform3f(m_shaderLocLightDir, -0.3f, -1.0f, -0.2f);
    glUniform3f(m_shaderLocLightColor, 1.0f, 1.0f, 1.0f);
    glUniform3f(m_shaderLocAmbientColor, 0.35f, 0.35f, 0.35f);
    glUniform3fv(m_shaderLocViewPos, 1, glm::value_ptr(m_cameraEye));

    const glm::mat4 mvp = m_proj * m_view * item.model;
    glUniformMatrix4fv(m_shaderLocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(m_shaderLocModel, 1, GL_FALSE, glm::value_ptr(item.model));
    glUniform4fv(m_shaderLocColor, 1, glm::value_ptr(item.color));

    EnsureMeshVAO(const_cast<GpuMesh&>(item.mesh)); // or item을 non-const로 설계
    GLuint vao = item.mesh.vao[m_currentWindowIndex];
    if (!vao) return;

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, item.mesh.indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

GLFWwindow* Renderer::GetWindow(uint32 index) const
{
    if (index >= m_windowCount) return nullptr;
    return m_windows[index];
}

void Renderer::SetCameraPos(const glm::vec3& eye)
{
    m_cameraEye = eye;
    m_view = glm::lookAt(m_cameraEye, m_cameraTarget, m_cameraUp);
}

void Renderer::SetCameraLookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
{
    m_cameraEye = eye;
    m_cameraTarget = target;
    m_cameraUp = up;
    m_view = glm::lookAt(m_cameraEye, m_cameraTarget, m_cameraUp);
}

void Renderer::SetPerspective(float fovDeg, float nearPlane, float farPlane)
{
    m_camFovDeg = fovDeg;
    m_camNear = nearPlane;
    m_camFar = farPlane;

    int fbw = 0, fbh = 0;
    if (GLFWwindow* current = glfwGetCurrentContext())
        glfwGetFramebufferSize(current, &fbw, &fbh);

    const float aspect = (fbw > 0 && fbh > 0) ? (static_cast<float>(fbw) / static_cast<float>(fbh)) : (4.0f / 3.0f);

    m_proj = glm::perspective(glm::radians(m_camFovDeg), aspect, m_camNear, m_camFar);
}

void Renderer::UpdateCamera(const glm::vec3& playerPos, float yaw, float pitch)
{
    glm::vec3 offset;
    offset.x = -sinf(yaw) * m_cameraDist;
    offset.y = 2.5f;
    offset.z = -cosf(yaw) * m_cameraDist;

    glm::vec3 cameraPos = playerPos + offset;
    glm::vec3 targetPos = playerPos + glm::vec3(0.0f, 1.0f, 0.0f);

    m_cameraEye = cameraPos; // Update camera position for lighting
    m_cameraTarget = targetPos;

    int fbw = 0, fbh = 0;
    if (GLFWwindow* current = glfwGetCurrentContext())
        glfwGetFramebufferSize(current, &fbw, &fbh);

    m_view = glm::lookAt(cameraPos, targetPos, m_cameraUp);

    const float aspect = (fbw > 0 && fbh > 0)
        ? (static_cast<float>(fbw) / static_cast<float>(fbh))
        : (4.0f / 3.0f);

    m_proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
}

void Renderer::FramebufferSizeCallbackStatic(GLFWwindow* window, int width, int height)
{
    if (Renderer* self = static_cast<Renderer*>(glfwGetWindowUserPointer(window)))
        self->FramebufferSizeCallback(window, width, height);
}

void Renderer::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    glfwMakeContextCurrent(window);

    glViewport(0, 0, width, height);

    const float aspect = (width > 0 && height > 0)
        ? (static_cast<float>(width) / static_cast<float>(height))
        : (4.0f / 3.0f);

    m_proj = glm::perspective(glm::radians(m_camFovDeg), aspect, m_camNear, m_camFar);
}

GLuint Renderer::CompileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        JAMNET_LOG_WARN("[Rendering] Shader Compile Error: {}", info);
    }

    return shader;
}

// ======================
// 버퍼 초기화 (공유)
// ======================

void Renderer::InitGrid()
{
    if (m_gridInitialized)
        return;

    std::vector<float> lines; // Position + Normal
    glm::vec3 normal(0.0f, 1.0f, 0.0f); // Up normal for grid

    for (int z = static_cast<int>(WORLD_RANGE_MIN); z <= static_cast<int>(WORLD_RANGE_MAX); z += static_cast<int>(WORLD_GRID_SIZE))
    {
        // First vertex
        lines.push_back(WORLD_RANGE_MIN);
        lines.push_back(0.05f);
        lines.push_back(static_cast<float>(z));
        lines.push_back(normal.x);
        lines.push_back(normal.y);
        lines.push_back(normal.z);
        // Second vertex
        lines.push_back(WORLD_RANGE_MAX);
        lines.push_back(0.05f);
        lines.push_back(static_cast<float>(z));
        lines.push_back(normal.x);
        lines.push_back(normal.y);
        lines.push_back(normal.z);
    }

    for (int x = static_cast<int>(WORLD_RANGE_MIN); x <= static_cast<int>(WORLD_RANGE_MAX); x += static_cast<int>(WORLD_GRID_SIZE))
    {
        // First vertex
        lines.push_back(static_cast<float>(x));
        lines.push_back(0.05f);
        lines.push_back(WORLD_RANGE_MIN);
        lines.push_back(normal.x);
        lines.push_back(normal.y);
        lines.push_back(normal.z);
        // Second vertex
        lines.push_back(static_cast<float>(x));
        lines.push_back(0.05f);
        lines.push_back(WORLD_RANGE_MAX);
        lines.push_back(normal.x);
        lines.push_back(normal.y);
        lines.push_back(normal.z);
    }

    glGenBuffers(1, &m_gridVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
    glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_STATIC_DRAW);

    m_gridVertexCount = static_cast<GLsizei>(lines.size() / 6); // 6 floats per vertex
    m_gridInitialized = true;
}

void Renderer::InitPlane()
{
    if (m_planeInitialized)
        return;

    float planeVertices[] = {
        // positions         // normals
        -1.0f, 0.0f, -1.0f,   0.0f, 1.0f, 0.0f,
         1.0f, 0.0f, -1.0f,   0.0f, 1.0f, 0.0f,
         1.0f, 0.0f,  1.0f,   0.0f, 1.0f, 0.0f,
        -1.0f, 0.0f,  1.0f,   0.0f, 1.0f, 0.0f,
    };

    unsigned int indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    glGenBuffers(1, &m_planeVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_planeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(planeVertices), planeVertices, GL_STATIC_DRAW);

    glGenBuffers(1, &m_planeEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_planeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    m_planeInitialized = true;
}

void Renderer::InitBox()
{
    if (m_boxInitialized)
        return;

    // Position + Normal (6 floats per vertex)
    float vertices[] = {
        // Front face (z = 0.5)
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // 0
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // 1
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // 2
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // 3
        // Back face (z = -0.5)
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // 4
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // 5
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // 6
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // 7
        // Left face (x = -0.5)
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f, // 8
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f, // 9
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f, // 10
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f, // 11
        // Right face (x = 0.5)
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f, // 12
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f, // 13
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f, // 14
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f, // 15
        // Top face (y = 0.5)
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, // 16
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f, // 17
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f, // 18
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, // 19
        // Bottom face (y = -0.5)
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f, // 20
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f, // 21
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f, // 22
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f  // 23
    };

    unsigned int indices[] = {
        0, 1, 2,  2, 3, 0,    // front
        4, 7, 6,  6, 5, 4,    // back
        8, 9, 10, 10, 11, 8,  // left
        12, 15, 14, 14, 13, 12, // right
        16, 17, 18, 18, 19, 16, // top
        20, 23, 22, 22, 21, 20  // bottom
    };

    glGenBuffers(1, &m_boxVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_boxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &m_boxEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_boxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    m_boxInitialized = true;
}

void Renderer::InitSphere()
{
    if (m_sphereInitialized)
        return;

    constexpr int sectorCount = 24;
    constexpr int stackCount  = 16;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    // Vertex (position + normal)
    for (int i = 0; i <= stackCount; ++i)
    {
        float stackAngle = glm::pi<float>() / 2 - static_cast<float>(i) / stackCount * glm::pi<float>();
        float xy = cosf(stackAngle);
        float z = sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j)
        {
            float sectorAngle = static_cast<float>(j) / sectorCount * glm::two_pi<float>();
            float x = xy * cosf(sectorAngle);
            float y = xy * sinf(sectorAngle);

            // Position
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            // Normal (same as position for unit sphere)
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }

    // Index
    for (int i = 0; i < stackCount; ++i)
    {
        int k1 = i * (sectorCount + 1);
        int k2 = k1 + sectorCount + 1;

        for (int j = 0; j < sectorCount; ++j)
        {
            indices.push_back(k1 + j);
            indices.push_back(k2 + j);
            indices.push_back(k1 + j + 1);

            indices.push_back(k1 + j + 1);
            indices.push_back(k2 + j);
            indices.push_back(k2 + j + 1);
        }
    }

    m_sphereIndexCount = static_cast<GLsizei>(indices.size());

    glGenBuffers(1, &m_sphereVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &m_sphereEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    m_sphereInitialized = true;
}

void Renderer::InitCapsule()
{
    if (m_capsuleInitialized)
        return;

    constexpr int sectorCount = 24;
    constexpr int stackCount = 16;

    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;

    // Cylinder body (y축 기준)
    float h = 1.0f;
    for (int i = 0; i <= 1; ++i)
    {
        float y = -h + i * 2 * h;
        for (int j = 0; j <= sectorCount; ++j)
        {
            float angle = static_cast<float>(j) / sectorCount * glm::two_pi<float>();
            float x = cosf(angle);
            float z = sinf(angle);
            vertices.emplace_back(x, y, z);
        }
    }

    // cylinder indices
    for (int j = 0; j < sectorCount; ++j)
    {
        int k1 = j;
        int k2 = j + (sectorCount + 1);

        indices.push_back(k1);
        indices.push_back(k2);
        indices.push_back(k1 + 1);

        indices.push_back(k1 + 1);
        indices.push_back(k2);
        indices.push_back(k2 + 1);
    }

    // Upper hemisphere
    int baseIndexUpper = static_cast<int>(vertices.size());
    for (int i = 0; i <= stackCount; ++i)
    {
        float stackAngle = static_cast<float>(i) / stackCount * (glm::pi<float>() / 2); // 0 → PI/2
        float xy = cosf(stackAngle);
        float z = sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j)
        {
            float angle = static_cast<float>(j) / sectorCount * glm::two_pi<float>();
            float x = xy * cosf(angle);
            float y = xy * sinf(angle);
            vertices.emplace_back(x, z + h, y);
        }
    }

    for (int i = 0; i < stackCount; ++i)
    {
        int k1 = baseIndexUpper + i * (sectorCount + 1);
        int k2 = k1 + (sectorCount + 1);

        for (int j = 0; j < sectorCount; ++j)
        {
            indices.push_back(k1 + j);
            indices.push_back(k2 + j);
            indices.push_back(k1 + j + 1);

            indices.push_back(k1 + j + 1);
            indices.push_back(k2 + j);
            indices.push_back(k2 + j + 1);
        }
    }

    // Lower hemisphere
    int baseIndexLower = static_cast<int>(vertices.size());
    for (int i = 0; i <= stackCount; ++i)
    {
        float stackAngle = static_cast<float>(i) / stackCount * (glm::pi<float>() / 2);
        float xy = cosf(stackAngle);
        float z = sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j)
        {
            float angle = static_cast<float>(j) / sectorCount * glm::two_pi<float>();
            float x = xy * cosf(angle);
            float y = xy * sinf(angle);
            vertices.emplace_back(x, -z - h, y);
        }
    }

    for (int i = 0; i < stackCount; ++i)
    {
        int k1 = baseIndexLower + i * (sectorCount + 1);
        int k2 = k1 + (sectorCount + 1);

        for (int j = 0; j < sectorCount; ++j)
        {
            indices.push_back(k1 + j);
            indices.push_back(k2 + j);
            indices.push_back(k1 + j + 1);

            indices.push_back(k1 + j + 1);
            indices.push_back(k2 + j);
            indices.push_back(k2 + j + 1);
        }
    }

    m_capsuleIndexCount = static_cast<GLsizei>(indices.size());

    glGenBuffers(1, &m_capsuleVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_capsuleVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &m_capsuleEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_capsuleEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    m_capsuleInitialized = true;
}

void Renderer::InitRay()
{
    if (m_rayInitialized)
        return;

    // Position + Normal (6 floats per vertex)
    float rayVertices[12] = {
        0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // First vertex
        0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f  // Second vertex
    };

    glGenBuffers(1, &m_rayVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_rayVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rayVertices), rayVertices, GL_DYNAMIC_DRAW);

    m_rayInitialized = true;
}

// ======================
// 윈도우별 VAO 생성
// ======================

void Renderer::EnsureGridVAO()
{
    if (!m_gridInitialized || m_currentWindowIndex < 0) return;
    uint32 idx = static_cast<uint32>(m_currentWindowIndex);
    if (idx >= m_windowCount) return;
    if (m_gridVAO[idx] != 0) return;

    glGenVertexArrays(1, &m_gridVAO[idx]);
    glBindVertexArray(m_gridVAO[idx]);

    glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), static_cast<void*>(0));
    glEnableVertexAttribArray(0);
    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void Renderer::EnsurePlaneVAO()
{
    if (!m_planeInitialized || m_currentWindowIndex < 0) return;
    uint32 idx = static_cast<uint32>(m_currentWindowIndex);
    if (idx >= m_windowCount) return;
    if (m_planeVAO[idx] != 0) return;

    glGenVertexArrays(1, &m_planeVAO[idx]);
    glBindVertexArray(m_planeVAO[idx]);

    glBindBuffer(GL_ARRAY_BUFFER, m_planeVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), static_cast<void*>(0));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_planeEBO);

    glBindVertexArray(0);
}

void Renderer::EnsureBoxVAO()
{
    if (!m_boxInitialized || m_currentWindowIndex < 0) return;
    uint32 idx = static_cast<uint32>(m_currentWindowIndex);
    if (idx >= m_windowCount) return;
    if (m_boxVAO[idx] != 0) return;

    glGenVertexArrays(1, &m_boxVAO[idx]);
    glBindVertexArray(m_boxVAO[idx]);

    glBindBuffer(GL_ARRAY_BUFFER, m_boxVBO);
    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), static_cast<void*>(0));
    glEnableVertexAttribArray(0);
    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_boxEBO);

    glBindVertexArray(0);
}

void Renderer::EnsureSphereVAO()
{
    if (!m_sphereInitialized || m_currentWindowIndex < 0) return;
    uint32 idx = static_cast<uint32>(m_currentWindowIndex);
    if (idx >= m_windowCount) return;
    if (m_sphereVAO[idx] != 0) return;

    glGenVertexArrays(1, &m_sphereVAO[idx]);
    glBindVertexArray(m_sphereVAO[idx]);

    glBindBuffer(GL_ARRAY_BUFFER, m_sphereVBO);
    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), static_cast<void*>(0));
    glEnableVertexAttribArray(0);
    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_sphereEBO);

    glBindVertexArray(0);
}

void Renderer::EnsureCapsuleVAO()
{
    if (!m_capsuleInitialized || m_currentWindowIndex < 0) return;
    uint32 idx = static_cast<uint32>(m_currentWindowIndex);
    if (idx >= m_windowCount) return;
    if (m_capsuleVAO[idx] != 0) return;

    glGenVertexArrays(1, &m_capsuleVAO[idx]);
    glBindVertexArray(m_capsuleVAO[idx]);

    glBindBuffer(GL_ARRAY_BUFFER, m_capsuleVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), static_cast<void*>(0));
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_capsuleEBO);

    glBindVertexArray(0);
}

void Renderer::EnsureRayVAO()
{
    if (!m_rayInitialized || m_currentWindowIndex < 0) return;
    uint32 idx = static_cast<uint32>(m_currentWindowIndex);
    if (idx >= m_windowCount) return;
    if (m_rayVAO[idx] != 0) return;

    glGenVertexArrays(1, &m_rayVAO[idx]);
    glBindVertexArray(m_rayVAO[idx]);

    glBindBuffer(GL_ARRAY_BUFFER, m_rayVBO);
    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), static_cast<void*>(0));
    glEnableVertexAttribArray(0);
    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void Renderer::EnsureMeshVAO(GpuMesh& m)
{
    if (m_currentWindowIndex < 0) return;
    uint32 idx = static_cast<uint32>(m_currentWindowIndex);
    if (idx >= m_windowCount) return;

    if (m.vao[idx] != 0) return;

    glGenVertexArrays(1, &m.vao[idx]);
    glBindVertexArray(m.vao[idx]);

    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), static_cast<void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    glBindVertexArray(0);
}

GpuMesh Renderer::CreateGpuMesh_PosNrm_IdxU32(const std::vector<float>& vtx, const std::vector<uint32_t>& idx)
{
    GpuMesh m{};

    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);

    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vtx.size() * sizeof(float)), vtx.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)), idx.data(), GL_STATIC_DRAW);

    // vao는 여기서 만들지 않는다!
    // m.vao[]는 0으로 남겨두고 Draw 시 EnsureMeshVAO에서 생성

    m.indexCount = static_cast<GLsizei>(idx.size());
    return m;
}

void Renderer::DestroyGpuMesh(GpuMesh& m)
{
    // 윈도우별 VAO 삭제
    for (uint32 i = 0; i < MAX_WINDOWS; ++i)
    {
        if (m.vao[i])
        {
            glDeleteVertexArrays(1, &m.vao[i]);
            m.vao[i] = 0;
        }
    }

    if (m.ebo) { glDeleteBuffers(1, &m.ebo); m.ebo = 0; }
    if (m.vbo) { glDeleteBuffers(1, &m.vbo); m.vbo = 0; }

    m.indexCount = 0;
}

bool Renderer::BuildItemsFromGLTF(const std::string& path)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = false;
    const bool isGlb = (path.size() >= 4 &&
        (path.substr(path.size() - 4) == ".glb" || path.substr(path.size() - 4) == ".GLB"));

    if (isGlb) ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    else       ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);

    PrintIfNotEmpty("WARN", warn);
    PrintIfNotEmpty("ERR", err);

    if (!ok)
    {
        JAMNET_LOG_WARN("[Rendering] LoadGLTF failed: {}", path);
        return false;
    }

    if (model.scenes.empty())
    {
        JAMNET_LOG_WARN("[Rendering] glTF has no scenes");
        return false;
    }

    const int sceneIndex = (model.defaultScene >= 0) ? model.defaultScene : 0;
    const auto& scene = model.scenes[sceneIndex];

    // 1) mesh/primitive 순서대로 RenderItem 생성
    for (const auto& mesh : model.meshes)
    {
        for (const auto& prim : mesh.primitives)
        {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            auto itPos = prim.attributes.find("POSITION");
            auto itNrm = prim.attributes.find("NORMAL");
            if (itPos == prim.attributes.end() || itNrm == prim.attributes.end())
                continue;

            const tinygltf::Accessor& posAcc = model.accessors[itPos->second];
            const tinygltf::Accessor& nrmAcc = model.accessors[itNrm->second];

            std::vector<glm::vec3> positions;
            std::vector<glm::vec3> normals;
            ReadVec3Float(model, posAcc, positions);
            ReadVec3Float(model, nrmAcc, normals);

            if (positions.size() != normals.size())
                throw std::runtime_error("POSITION/NORMAL count mismatch");

            std::vector<uint32_t> indices;
            if (prim.indices >= 0)
            {
                const tinygltf::Accessor& idxAcc = model.accessors[prim.indices];
                ReadIndicesU32(model, idxAcc, indices);
            }
            else
            {
                indices.resize(positions.size());
                for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) indices[i] = i;
            }

            std::vector<float> vtx;
            vtx.reserve(positions.size() * 6);

            for (size_t i = 0; i < positions.size(); ++i)
            {
                vtx.push_back(positions[i].x);
                vtx.push_back(positions[i].y);
                vtx.push_back(positions[i].z);
                vtx.push_back(normals[i].x);
                vtx.push_back(normals[i].y);
                vtx.push_back(normals[i].z);
            }

            RenderItem item;
            item.mesh = CreateGpuMesh_PosNrm_IdxU32(vtx, indices);
            item.model = glm::mat4(1.0f);
            item.color = glm::vec4(0.85f, 0.85f, 0.85f, 1.0f);

            m_gltfItems.push_back(item);
        }
    }

    // 2) scene graph traverse로 각 primitive에 world transform 매핑
    size_t primCursor = 0;
    for (int nodeIndex : scene.nodes)
        TraverseNode(model, nodeIndex, glm::mat4(1.0f), primCursor);

    JAMNET_LOG_INFO("[Rendering] glTF loaded: {} (items={})", path, m_gltfItems.size());
    return true;
}

glm::mat4 Renderer::GetNodeLocalMatrix(const tinygltf::Node& n) const
{
    glm::mat4 m(1.0f);

    if (!n.matrix.empty())
    {
        // glTF stores column-major 4x4
        m = glm::make_mat4(n.matrix.data());
        return m;
    }

    if (!n.translation.empty())
        m = glm::translate(m, glm::vec3(static_cast<float>(n.translation[0]), static_cast<float>(n.translation[1]), static_cast<float>(n.translation[2])));

    if (!n.rotation.empty())
    {
        // glTF rotation is [x,y,z,w]
        glm::quat q(static_cast<float>(n.rotation[3]), static_cast<float>(n.rotation[0]), static_cast<float>(n.rotation[1]), static_cast<float>(n.rotation[2]));
        m *= glm::mat4_cast(q);
    }

    if (!n.scale.empty())
        m = glm::scale(m, glm::vec3(static_cast<float>(n.scale[0]), static_cast<float>(n.scale[1]), static_cast<float>(n.scale[2])));

    return m;
}

void Renderer::TraverseNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parent, size_t& primCursor)
{
    const auto& n = model.nodes[nodeIndex];
    glm::mat4 world = parent * GetNodeLocalMatrix(n);

    if (n.mesh >= 0)
    {
        const auto& mesh = model.meshes[n.mesh];
        for (size_t i = 0; i < mesh.primitives.size(); ++i)
        {
            if (primCursor >= m_gltfItems.size()) return;
            m_gltfItems[primCursor++].model = world;
        }
    }

    for (int c : n.children)
        TraverseNode(model, c, world, primCursor);
}
