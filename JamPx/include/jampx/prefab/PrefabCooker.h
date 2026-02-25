#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace jam::px::prefab
{
	using nlohmann::json;

	class PrefabCooker final
	{
	public:
		
		///@brief cook single triangle mesh from *.gltf or *.glb
		static void CookTriangleMesh(const string& gltfPath, const string& outPxtriPath, int32 meshIndex, int32 primitiveIndex);
		
		///@brief cook scene-merged triangle mesh from *.gltf or *.glb
		static void CookTriangleMesh(const string& gltfPath, const string& outPxtriPath);

		///@brief cook single convex mesh from *.gltf or *.glb
		static void CookConvexMesh(const string& gltfPath, const string& outPxcvxPath, int32 meshIndex, int32 primitiveIndex);
	
		///@brief cook scene-merged convex mesh from *.gltf or *.glb
		static void CookConvexMesh(const string& gltfPath, const string& outPxcvcPath);
	};

}
