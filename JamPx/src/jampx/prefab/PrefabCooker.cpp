#include "pch.h"
#include "jampx/prefab/PrefabCooker.h"
#include "jampx/prefab/PhysicsPrefabIO.h"

#include <algorithm>
#include <execution>
#include <fstream>
#include <iostream>
#include <tiny_gltf.h>

namespace jam::px
{
	namespace
	{
		struct ExtractedMesh
		{
			std::vector<PxVec3> positions;
			std::vector<uint32> indices;
		};

		static bool WriteAllBytes(const std::string& path, const void* data, size_t size)
		{
			std::ofstream f(path, std::ios::binary);
			if (!f) return false;
			f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
			return true;
		}

		static bool FileExists(const std::string& path)
		{
			std::ifstream f(path, std::ios::binary);
			return static_cast<bool>(f);
		}

		static void EnsureParentDirExists(const std::string& path)
		{
			namespace fs = std::filesystem;

			const fs::path p(path);
			const fs::path parent = p.parent_path();
			if (parent.empty())
				return;

			std::error_code ec;
			fs::create_directories(parent, ec);
			if (ec)
				throw std::runtime_error("failed to create directories: " + parent.string());
		}

		static bool LoadGltfModel(const std::string& path, OUT tinygltf::Model& outModel, OUT std::string& outErr)
		{
			tinygltf::TinyGLTF loader;
			std::string err, warn;

			const bool isGlb = path.size() >= 4 && (path.substr(path.size() - 4) == ".glb" || path.substr(path.size() - 4) == ".GLB");

			bool ok = false;
			if (isGlb) ok = loader.LoadBinaryFromFile(&outModel, &err, &warn, path);
			else       ok = loader.LoadASCIIFromFile(&outModel, &err, &warn, path);

			if (!ok)
			{
				outErr = "tinygltf load failed: " + path;
				if (!err.empty()) outErr += "\nerr: " + err;
				return false;
			}

			return true;
		}

		static const tinygltf::Accessor* GetAccessor(const tinygltf::Model& model, int32 idx)
		{
			if (idx < 0 || idx >= static_cast<int>(model.accessors.size())) return nullptr;
			return &model.accessors[idx];
		}

		static const tinygltf::BufferView* GetBufferView(const tinygltf::Model& model, int32 idx)
		{
			if (idx < 0 || idx >= static_cast<int>(model.bufferViews.size())) return nullptr;
			return &model.bufferViews[idx];
		}

		static const tinygltf::Buffer* GetBuffer(const tinygltf::Model& model, int32 idx)
		{
			if (idx < 0 || idx >= static_cast<int>(model.buffers.size())) return nullptr;
			return &model.buffers[idx];
		}

		static PxMat44 MakeNodeLocalMatrix(const tinygltf::Node& n)
		{
			// matrix 우선
			if (n.matrix.size() == 16)
			{
				const double* m = n.matrix.data();

				return {
					PxVec4(static_cast<float>(m[0]), static_cast<float>(m[1]), static_cast<float>(m[2]), static_cast<float>(m[3])),
					PxVec4(static_cast<float>(m[4]), static_cast<float>(m[5]), static_cast<float>(m[6]), static_cast<float>(m[7])),
					PxVec4(static_cast<float>(m[8]), static_cast<float>(m[9]), static_cast<float>(m[10]), static_cast<float>(m[11])),
					PxVec4(static_cast<float>(m[12]), static_cast<float>(m[13]), static_cast<float>(m[14]), static_cast<float>(m[15]))
				};
			}

			PxVec3 T(0, 0, 0);
			if (n.translation.size() == 3)
				T = PxVec3(static_cast<float>(n.translation[0]), static_cast<float>(n.translation[1]), static_cast<float>(n.translation[2]));

			PxQuat R(physx::PxIdentity);
			if (n.rotation.size() == 4)
				R = PxQuat(static_cast<float>(n.rotation[0]), static_cast<float>(n.rotation[1]), static_cast<float>(n.rotation[2]), static_cast<float>(n.rotation[3]));

			PxVec3 S(1, 1, 1);
			if (n.scale.size() == 3)
				S = PxVec3(static_cast<float>(n.scale[0]), static_cast<float>(n.scale[1]), static_cast<float>(n.scale[2]));

			PxMat44 M(PxTransform(T, R));
			M.column0 *= S.x;
			M.column1 *= S.y;
			M.column2 *= S.z;

			return M;
		}

		static void TraverseNodeRecursive(const tinygltf::Model& model, int32 nodeIndex, const PxMat44& parent, const std::function<void(int, const PxMat44&)>& onVisit)
		{
			const auto& node = model.nodes[nodeIndex];
			const PxMat44 local = MakeNodeLocalMatrix(node);
			const PxMat44 global = parent * local;

			onVisit(nodeIndex, global);

			for (int child : node.children)
				TraverseNodeRecursive(model, child, global, onVisit);
		}



		static bool ExtractPositions(const tinygltf::Model& model, const tinygltf::Accessor& acc, OUT std::vector<PxVec3>& out, OUT std::string& outErr)
		{
			if (acc.type != TINYGLTF_TYPE_VEC3 || acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
			{
				outErr = "POSITION accessor must be VEC3/FLOAT";
				return false;
			}

			const auto* bv = GetBufferView(model, acc.bufferView);
			if (!bv) { outErr = "invalid POSITION bufferView"; return false; }

			const auto* buf = GetBuffer(model, bv->buffer);
			if (!buf) { outErr = "invalid POSITION buffer"; return false; }

			const size_t stride = acc.ByteStride(*bv);
			constexpr size_t elemSize = sizeof(float) * 3;
			if (stride < elemSize) { outErr = "POSITION stride too small"; return false; }

			const size_t baseOffset = static_cast<size_t>(bv->byteOffset) + static_cast<size_t>(acc.byteOffset);
			const size_t count = static_cast<size_t>(acc.count);

			out.resize(count);

			for (size_t i = 0; i < count; ++i)
			{
				const size_t off = baseOffset + i * stride;
				if (off + elemSize > buf->data.size()) { outErr = "POSITION buffer overflow"; return false; }

				const float* p = reinterpret_cast<const float*>(&buf->data[off]);
				out[i] = PxVec3(p[0], p[1], p[2]);
			}

			return true;
		}

		static bool ExtractIndices(const tinygltf::Model& model, const tinygltf::Accessor& acc, OUT std::vector<uint32_t>& out, OUT std::string& outErr)
		{
			if (acc.type != TINYGLTF_TYPE_SCALAR) { outErr = "indices accessor must be SCALAR"; return false; }

			if (acc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT && acc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
			{
				outErr = "indices componentType must be UNSIGNED_SHORT or UNSIGNED_INT";
				return false;
			}

			const auto* bv = GetBufferView(model, acc.bufferView);
			if (!bv) { outErr = "invalid indices bufferView"; return false; }

			const auto* buf = GetBuffer(model, bv->buffer);
			if (!buf) { outErr = "invalid indices buffer"; return false; }

			const size_t compSize = (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) ? sizeof(uint16_t) : sizeof(uint32_t);

			const size_t byteStep = compSize;

			const size_t baseOffset = static_cast<size_t>(bv->byteOffset) + static_cast<size_t>(acc.byteOffset);
			const size_t count = static_cast<size_t>(acc.count);

			if (baseOffset + count * compSize > buf->data.size())
			{
				outErr = "indices buffer overflow";
				return false;
			}

			out.resize(count);

			for (size_t i = 0; i < count; ++i)
			{
				const size_t off = baseOffset + i * byteStep;

				if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					out[i] = static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(&buf->data[off]));
				else
					out[i] = static_cast<uint32_t>(*reinterpret_cast<const uint32_t*>(&buf->data[off]));
			}

			return true;
		}

		static bool ExtractSingleMesh(const std::string& gltfPath, int32 meshIndex, int32 primitiveIndex, OUT ExtractedMesh& out, OUT std::string& outErr)
		{
			out = {};

			tinygltf::Model model;
			if (!LoadGltfModel(gltfPath, model, outErr))
				return false;

			if (meshIndex < 0 || meshIndex >= static_cast<int>(model.meshes.size()))
			{
				outErr = "invalid mesh_index";
				return false;
			}

			const auto& mesh = model.meshes[meshIndex];
			if (primitiveIndex < 0 || primitiveIndex >= static_cast<int>(mesh.primitives.size()))
			{
				outErr = "invalid primitive_index";
				return false;
			}

			const auto& prim = mesh.primitives[primitiveIndex];
			if (prim.mode != TINYGLTF_MODE_TRIANGLES)
			{
				outErr = "only TRIANGLES primitive is supported";
				return false;
			}

			auto itPos = prim.attributes.find("POSITION");
			if (itPos == prim.attributes.end())
			{
				outErr = "POSITION missing";
				return false;
			}

			const auto* posAcc = GetAccessor(model, itPos->second);
			if (!posAcc) { outErr = "invalid POSITION accessor"; return false; }

			if (!ExtractPositions(model, *posAcc, out.positions, outErr))
				return false;

			if (prim.indices < 0)
			{
				outErr = "indices missing";
				return false;
			}

			const auto* idxAcc = GetAccessor(model, prim.indices);
			if (!idxAcc) { outErr = "invalid indices accessor"; return false; }

			if (!ExtractIndices(model, *idxAcc, out.indices, outErr))
				return false;

			if ((out.indices.size() % 3) != 0)
			{
				outErr = "indices must be multiple of 3";
				return false;
			}

			return true;
		}

		static bool ExtractMergedSceneMesh(const std::string& gltfPath, OUT ExtractedMesh& out, OUT std::string& outErr)
		{
			out = {};

			tinygltf::Model model;
			if (!LoadGltfModel(gltfPath, model, outErr))
				return false;

			int sceneIndex = model.defaultScene;
			sceneIndex = std::max(sceneIndex, 0);
			if (sceneIndex < 0 || sceneIndex >= static_cast<int32>(model.scenes.size()))
			{
				outErr = "no valid scene";
				return false;
			}

			const auto& scene = model.scenes[sceneIndex];
			const PxMat44 I(physx::PxIdentity);

			auto appendPrim = [&](const tinygltf::Primitive& prim, const PxMat44& nodeGlobal)
				{
					if (prim.mode != TINYGLTF_MODE_TRIANGLES)
						return;

					auto itPos = prim.attributes.find("POSITION");
					if (itPos == prim.attributes.end())
						return;

					const auto& posAcc = model.accessors[itPos->second];

					std::vector<PxVec3> positions;
					std::string err;
					if (!ExtractPositions(model, posAcc, positions, err))
						throw std::runtime_error(err);

					const auto& idxAcc = model.accessors[prim.indices];
					std::vector<uint32> indices;
					if (!ExtractIndices(model, idxAcc, indices, err))
						throw std::runtime_error(err);

					const uint32 base = static_cast<uint32>(out.positions.size());

					for (const auto& p : positions)
					{
						PxVec4 v(p.x, p.y, p.z, 1.0f);
						PxVec4 r = nodeGlobal.transform(v);
						out.positions.emplace_back(r.x, r.y, r.z);
					}

					for (uint32 i : indices)
						out.indices.push_back(base + i);
				};

			for (int root : scene.nodes)
			{
				TraverseNodeRecursive(model, root, I,
					[&](int nodeIndex, const PxMat44& global)
					{
						const auto& node = model.nodes[nodeIndex];
						if (node.mesh < 0) return;

						const auto& mesh = model.meshes[node.mesh];
						for (const auto& prim : mesh.primitives)
							appendPrim(prim, global);
					});
			}

			if (out.positions.empty() || out.indices.empty())
			{
				outErr = "merged scene empty";
				return false;
			}

			return true;
		}

		static physx::PxCookingParams MakeDefaultCookingParams()
		{
			PxTolerancesScale scale{};
			physx::PxCookingParams params(scale);
			params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eWELD_VERTICES;
			params.meshWeldTolerance = EPSILON;
			return params;
		}

		static void DebugMeshStats(const ExtractedMesh& mesh)
		{
			PxVec3 mn(FLT_MAX), mx(-FLT_MAX);
			for (const auto& p : mesh.positions)
			{
				mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
				mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
			}
			const PxVec3 ext = mx - mn;

			float maxEdge = 0.0f;
			const size_t triCount = mesh.indices.size() / 3;
			for (size_t t = 0; t < triCount; ++t)
			{
				const PxVec3 a = mesh.positions[mesh.indices[t * 3 + 0]];
				const PxVec3 b = mesh.positions[mesh.indices[t * 3 + 1]];
				const PxVec3 c = mesh.positions[mesh.indices[t * 3 + 2]];
				maxEdge = std::max(maxEdge, (a - b).magnitude());
				maxEdge = std::max(maxEdge, (b - c).magnitude());
				maxEdge = std::max(maxEdge, (c - a).magnitude());
			}

			std::printf("[Cook] verts=%zu tris=%zu\n", mesh.positions.size(), triCount);
			std::printf("[Cook] AABB min(%.3f %.3f %.3f) max(%.3f %.3f %.3f) ext(%.3f %.3f %.3f)\n",
				mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, ext.x, ext.y, ext.z);
			std::printf("[Cook] maxEdge=%.3f\n", maxEdge);
		}

		static void DebugWorstTriangle(const ExtractedMesh& mesh)
		{
			float best = -1.0f;
			size_t bestT = 0;
			int bestE = 0;

			const size_t triCount = mesh.indices.size() / 3;
			for (size_t t = 0; t < triCount; ++t)
			{
				const uint32 ia = mesh.indices[t * 3 + 0];
				const uint32 ib = mesh.indices[t * 3 + 1];
				const uint32 ic = mesh.indices[t * 3 + 2];

				const PxVec3 a = mesh.positions[ia];
				const PxVec3 b = mesh.positions[ib];
				const PxVec3 c = mesh.positions[ic];

				const float ab = (a - b).magnitude();
				const float bc = (b - c).magnitude();
				const float ca = (c - a).magnitude();

				float m = ab; int e = 0;
				if (bc > m) { m = bc; e = 1; }
				if (ca > m) { m = ca; e = 2; }

				if (m > best) { best = m; bestT = t; bestE = e; }
			}

			const uint32 ia = mesh.indices[bestT * 3 + 0];
			const uint32 ib = mesh.indices[bestT * 3 + 1];
			const uint32 ic = mesh.indices[bestT * 3 + 2];

			const PxVec3 a = mesh.positions[ia];
			const PxVec3 b = mesh.positions[ib];
			const PxVec3 c = mesh.positions[ic];

			std::printf("[Cook] WORST tri=%zu maxEdge=%.3f edge=%d\n", bestT, best, bestE);
			std::printf("  ia=%u (%.3f %.3f %.3f)\n", ia, a.x, a.y, a.z);
			std::printf("  ib=%u (%.3f %.3f %.3f)\n", ib, b.x, b.y, b.z);
			std::printf("  ic=%u (%.3f %.3f %.3f)\n", ic, c.x, c.y, c.z);
		}

		static void CookTriangleMeshToPxtri(const physx::PxCookingParams& params, const ExtractedMesh& mesh, const std::string& outPath)
		{
			DebugMeshStats(mesh);
			DebugWorstTriangle(mesh);

			PxTriangleMeshDesc desc;
			desc.points.count = static_cast<PxU32>(mesh.positions.size());
			desc.points.stride = sizeof(PxVec3);
			desc.points.data = mesh.positions.data();

			desc.triangles.count = static_cast<PxU32>(mesh.indices.size() / 3);
			desc.triangles.stride = sizeof(uint32_t) * 3;
			desc.triangles.data = mesh.indices.data();

			physx::PxDefaultMemoryOutputStream stream;
			PxTriangleMeshCookingResult::Enum result = PxTriangleMeshCookingResult::eFAILURE;

			const bool ok = PxCookTriangleMesh(params, desc, stream, &result);
			if (!ok || result != PxTriangleMeshCookingResult::eSUCCESS)
				throw std::runtime_error("PxCookTriangleMesh failed");

			EnsureParentDirExists(outPath);

			if (!WriteAllBytes(outPath, stream.getData(), stream.getSize()))
				throw std::runtime_error("failed to write: " + outPath);
		}

		static void CookConvexMeshToPxcvx(const PxCookingParams& params, const ExtractedMesh& mesh, const std::string& outPath)
		{
			PxConvexMeshDesc desc;
			desc.points.count = static_cast<PxU32>(mesh.positions.size());
			desc.points.stride = sizeof(PxVec3);
			desc.points.data = mesh.positions.data();
			desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;

			physx::PxDefaultMemoryOutputStream stream;
			PxConvexMeshCookingResult::Enum result = PxConvexMeshCookingResult::eFAILURE;

			const bool ok = PxCookConvexMesh(params, desc, stream, &result);
			if (!ok || result == PxConvexMeshCookingResult::eFAILURE)
				throw std::runtime_error("PxCookConvexMesh failed");

			EnsureParentDirExists(outPath);

			if (!WriteAllBytes(outPath, stream.getData(), stream.getSize()))
				throw std::runtime_error("failed to write: " + outPath);
		}

		static bool IsTriangleMeshType(const std::string& type)
		{
			return type == k_shapeTriangleMesh;
		}

		static bool IsConvexMeshType(const std::string& type)
		{
			return type == k_shapeConvexMesh;
		}

		static void CookShapeIfMesh(json& shape, const PxCookingParams& params)
		{
			const std::string type = shape.value(k_shapeType, "");
			if (!IsTriangleMeshType(type) && !IsConvexMeshType(type))
				return;

			if (!shape.contains(k_mesh) || !shape.at(k_mesh).is_object())
				throw std::runtime_error("mesh shape requires mesh object");

			json& mj = shape.at(k_mesh);

			const std::string src		= mj.value(k_src, "");
			const int32  meshIndex		= mj.value(k_meshIndex, 0);
			const int32  primIndex		= mj.value(k_primitiveIndex, 0);
			const std::string cooked	= mj.value(k_cooked, "");

			if (cooked.empty())
				throw std::runtime_error("mesh.cooked is required");

			if (src.empty())
				throw std::runtime_error("mesh.src is required for cooking");

			if (FileExists(cooked))
				return;

			ExtractedMesh em;
			std::string err;
			if (!ExtractSingleMesh(src, meshIndex, primIndex, em, err))
				throw std::runtime_error(err);

			if (IsTriangleMeshType(type))
				CookTriangleMeshToPxtri(params, em, cooked);
			else
				CookConvexMeshToPxcvx(params, em, cooked);
		}



	}


	void PrefabCooker::CookTriangleMesh(const std::string& gltfPath, const std::string& outPxtriPath, int32 meshIndex, int32 primitiveIndex)
	{
		if (gltfPath.empty())
			throw std::runtime_error("gltfPath is empty");
		if (outPxtriPath.empty())
			throw std::runtime_error("outPxtriPath is empty");

		ExtractedMesh em;
		std::string err;
		if (!ExtractSingleMesh(gltfPath, meshIndex, primitiveIndex, em, err))
			throw std::runtime_error(err);

		const PxCookingParams params = MakeDefaultCookingParams();
		CookTriangleMeshToPxtri(params, em, outPxtriPath);
	}

	void PrefabCooker::CookTriangleMesh(const std::string& gltfPath, const std::string& outPxtriPath)
	{
		if (gltfPath.empty())
			throw std::runtime_error("gltfPath is empty");
		if (outPxtriPath.empty())
			throw std::runtime_error("outPxtriPath is empty");

		ExtractedMesh em;
		std::string err;
		if (!ExtractMergedSceneMesh(gltfPath, em, err))
			throw std::runtime_error(err);

		const PxCookingParams params = MakeDefaultCookingParams();
		CookTriangleMeshToPxtri(params, em, outPxtriPath);
	}

	void PrefabCooker::CookConvexMesh(const std::string& gltfPath, const std::string& outPxcvxPath, int32 meshIndex, int32 primitiveIndex)
	{
		if (gltfPath.empty())
			throw std::runtime_error("gltfPath is empty");
		if (outPxcvxPath.empty())
			throw std::runtime_error("outPxcvxPath is empty");

		ExtractedMesh em;
		if (std::string err; !ExtractSingleMesh(gltfPath, meshIndex, primitiveIndex, em, err))
			throw std::runtime_error(err);

		const PxCookingParams params = MakeDefaultCookingParams();
		CookConvexMeshToPxcvx(params, em, outPxcvxPath);
	}

	void PrefabCooker::CookConvexMesh(const std::string& gltfPath, const std::string& outPxcvcPath)
	{
		if (gltfPath.empty())
			throw std::runtime_error("gltfPath is empty");
		if (outPxcvcPath.empty())
			throw std::runtime_error("outPxcvxPath is empty");

		ExtractedMesh em;
		std::string err;
		if (!ExtractMergedSceneMesh(gltfPath, em, err))
			throw std::runtime_error(err);

		const PxCookingParams params = MakeDefaultCookingParams();
		CookConvexMeshToPxcvx(params, em, outPxcvcPath);
	}



} // namespace jam::px
