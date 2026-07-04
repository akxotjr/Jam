#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace jam::px
{
	using nlohmann::json;

	class PxCooker final
	{
	public:
		
		///@brief cook single triangle mesh from *.gltf or *.glb
		static void CookTriangleMesh(const std::string& gltfPath, const std::string& outPxtriPath, int32 meshIndex, int32 primitiveIndex);
		
		///@brief cook scene-merged triangle mesh from *.gltf or *.glb
		static void CookTriangleMesh(const std::string& gltfPath, const std::string& outPxtriPath);

		///@brief cook single convex mesh from *.gltf or *.glb
		static void CookConvexMesh(const std::string& gltfPath, const std::string& outPxcvxPath, int32 meshIndex, int32 primitiveIndex);
	
		///@brief cook scene-merged convex mesh from *.gltf or *.glb
		static void CookConvexMesh(const std::string& gltfPath, const std::string& outPxcvcPath);
	};

} // namespace jam::px
