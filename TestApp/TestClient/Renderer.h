#pragma once

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Shell32.lib")


#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


namespace tinygltf
{
	class Node;
	class Model;
}

static constexpr float  WORLD_RANGE_MIN = -5000.f;
static constexpr float  WORLD_RANGE_MAX = 5000.f;
static constexpr float  WORLD_GRID_SIZE = 100.f;
static constexpr uint32 MAX_WINDOWS = 4;

struct SearchUserUIData;
struct FriendUIData;
struct RoomUIData;
struct UserProfileUIData;

struct GpuMesh
{
	GLuint  vbo = 0;
	GLuint  ebo = 0;
	GLsizei indexCount = 0;

	GLuint  vao[MAX_WINDOWS] = {}; // 윈도우별 VAO
};

struct RenderItem
{
	GpuMesh mesh;
	glm::mat4 model = glm::mat4(1.0f);
	glm::vec4 color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
};


class Renderer
{
	DECLARE_SINGLETON(Renderer)

public:
	void        Init(uint32 numWindows = 1);
	void        Shutdown();

	bool        ShouldClose() const;

	// 현재 그릴 윈도우 선택 + 클리어
	void        PreRender(uint32 index = 0);
	void        PostRender(uint32 index = 0);

	// 3D 디버그 드로잉
	void        DrawGrid(const glm::vec4& color);
	void        DrawPlane(const glm::vec3& position, const glm::vec2& scale, const glm::vec4& color);
	void        DrawBox(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale, const glm::vec4& color);
	void        DrawSphere(const glm::vec3& position, float radius, const glm::vec4& color);
	void        DrawCapsule(const glm::vec3& position, float radius, float halfHeight, const glm::vec4& color);
	void        DrawRay(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color);
	void        DrawGridPlaneBox(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale, const glm::vec4& boxColor, const glm::vec4& gridColor, float gridCellSize);

	// glTF 렌더링용 (추가)
	bool        LoadGLTFScene(const std::string& path);
	void        ClearGLTFScene();
	void        DrawLoadedGLTF(const glm::vec4* colorOverride = nullptr);

	void        DrawStaticMesh(const RenderItem& item);


	GLFWwindow* GetWindow(uint32 index = 0) const;
	int32       GetWindowCount() const { return static_cast<int32>(m_windowCount); }
	bool        ScreenPointToWorldRay(uint32 index, double mouseX, double mouseY, OUT glm::vec3& outOrigin, OUT glm::vec3& outDir) const;

	// 카메라 / 투영 설정 (현재 컨텍스트 기준)
	void        SetCameraPos(const glm::vec3& eye);
	void        SetCameraLookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0));
	void        SetPerspective(float fovDeg, float nearPlane, float farPlane);

	/// @brief 현재 컨텍스트 기준으로 동작
	void        UpdateCamera(const glm::vec3& playerPos, float yaw, float pitch);

private:
	static void FramebufferSizeCallbackStatic(GLFWwindow* window, int width, int height);
	void        FramebufferSizeCallback(GLFWwindow* window, int width, int height);
	GLuint      CompileShader(GLenum type, const char* source);

	// "공유 버퍼"(VBO/EBO) 초기화 – 컨텍스트 공유 리스트에 올라감
	void        InitGrid();
	void        InitPlane();
	void        InitBox();
	void        InitSphere();
	void        InitCapsule();
	void        InitRay();
	void        InitGridPlaneBox();

	// 현재 윈도우(컨텍스트)에 필요한 VAO가 없으면 생성
	void        EnsureGridVAO();
	void        EnsurePlaneVAO();
	void        EnsureBoxVAO();
	void        EnsureSphereVAO();
	void        EnsureCapsuleVAO();
	void        EnsureRayVAO();
	void        EnsureMeshVAO(GpuMesh& m) const;
	void        EnsureGridPlaneBoxVAO();

	// ===== glTF helpers (추가) =====
	GpuMesh     CreateGpuMesh_PosNrm_IdxU32(const std::vector<float>& interleavedPosNrm, const std::vector<uint32_t>& indices);
	void        DestroyGpuMesh(GpuMesh& m);

	bool        BuildItemsFromGLTF(const std::string& path);
	glm::mat4   GetNodeLocalMatrix(const tinygltf::Node& n) const;
	void        TraverseNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parent, size_t& primCursor);

private:
	USE_LOCK

	const char* m_vertexShaderSource = nullptr;
	const char* m_fragmentShaderSource = nullptr;

	GLFWwindow* m_windows[MAX_WINDOWS] = { nullptr, };
	uint32      m_windowCount = 0;
	int32       m_currentWindowIndex = -1;   // PreRender에서 설정

	GLuint      m_shaderProgram = 0;
	GLint       m_shaderLocMVP = -1;
	GLint       m_shaderLocColor = -1;
	GLint       m_shaderLocModel = -1;
	GLint       m_shaderLocLightDir = -1;
	GLint       m_shaderLocLightColor = -1;
	GLint       m_shaderLocAmbientColor = -1;
	GLint       m_shaderLocViewPos = -1;

	// ========= 공유 버퍼 (컨텍스트 공유) =========
	// Grid
	GLuint      m_gridVBO = 0;
	GLsizei     m_gridVertexCount = 0;
	bool        m_gridInitialized = false;
	// 각 윈도우별 VAO
	GLuint      m_gridVAO[MAX_WINDOWS] = { 0, };

	// Plane
	GLuint      m_planeVBO = 0;
	GLuint      m_planeEBO = 0;
	bool        m_planeInitialized = false;
	GLuint      m_planeVAO[MAX_WINDOWS] = { 0, };

	// Box
	GLuint      m_boxVBO = 0;
	GLuint      m_boxEBO = 0;
	bool        m_boxInitialized = false;
	GLuint      m_boxVAO[MAX_WINDOWS] = { 0, };

	// Sphere
	GLuint      m_sphereVBO = 0;
	GLuint      m_sphereEBO = 0;
	GLsizei     m_sphereIndexCount = 0;
	bool        m_sphereInitialized = false;
	GLuint      m_sphereVAO[MAX_WINDOWS] = { 0, };

	// Capsule
	GLuint      m_capsuleVBO = 0;
	GLuint      m_capsuleEBO = 0;
	GLsizei     m_capsuleIndexCount = 0;
	bool        m_capsuleInitialized = false;
	GLuint      m_capsuleVAO[MAX_WINDOWS] = { 0, };

	// Ray
	GLuint      m_rayVBO                        = 0;
	bool        m_rayInitialized                = false;
	GLuint      m_rayVAO[MAX_WINDOWS]           = { 0, };

	// GridPlaneBox overlay line buffer
	GLuint      m_gridPlaneBoxVBO               = 0;
	bool        m_gridPlaneBoxInitialized       = false;
	GLuint      m_gridPlaneBoxVAO[MAX_WINDOWS]  = { 0, };

	// Crosshair (나중에 쓸 수 있으니 남겨둠)
	GLuint      m_crossVBO = 0;
	bool        m_crosshairInitialized = false;
	GLuint      m_crossVAO[MAX_WINDOWS] = { 0, };

	// 카메라 / 투영 (전역 – 현재는 모든 윈도우에서 공유, 필요하면 per-window로 확장)
	glm::mat4               m_view = glm::mat4(1.0f);
	glm::mat4               m_proj = glm::mat4(1.0f);
	glm::mat4               m_viewByWindow[MAX_WINDOWS] = {};
	glm::mat4               m_projByWindow[MAX_WINDOWS] = {};
	bool                    m_hasViewByWindow[MAX_WINDOWS] = { false, };
	bool                    m_hasProjByWindow[MAX_WINDOWS] = { false, };

	glm::vec3               m_cameraUp = { 0, 1, 0 };
	glm::vec4               m_cameraOffset = { -0.2f, 0.4f, -1.5f , 1.0f };
	float                   m_cameraDist = 10.f;

	glm::vec3               m_cameraEye = { 0.0f, 2.5f, 6.0f };
	glm::vec3               m_cameraTarget = { 0.0f, 0.5f, 0.0f };
	float                   m_camFovDeg = 45.0f;

	float                   m_camNear = 0.1f;
	float                   m_camFar = 100.0f;

	std::vector<RenderItem> m_gltfItems;
};
