// Lightweight header to expose DirectXMath types via simple aliases
#pragma once

#include <DirectXMath.h>
#include <DirectXCollision.h>

// Bring commonly-used DirectXMath types into the project's global namespace
using XMVECTOR = DirectX::XMVECTOR;
using XMMATRIX = DirectX::XMMATRIX;

using XMFLOAT2 = DirectX::XMFLOAT2;
using XMFLOAT3 = DirectX::XMFLOAT3;
using XMFLOAT4 = DirectX::XMFLOAT4;
using XMFLOAT3A = DirectX::XMFLOAT3A;
using XMFLOAT4A = DirectX::XMFLOAT4A;
using XMFLOAT4X4 = DirectX::XMFLOAT4X4;

// Additional convenience aliases
using Vector = XMVECTOR;
using Matrix = XMMATRIX;

// Common float-vector aliases
using Vector2 = XMFLOAT2;
using Vector3 = XMFLOAT3;
using Vector4 = XMFLOAT4;
using Vector3A = XMFLOAT3A;
using Vector4A = XMFLOAT4A;
using Vector4x4 = XMFLOAT4X4;

// Integer / unsigned variants
using Vector2U = DirectX::XMUINT2;
using Vector2I = DirectX::XMINT2;
using Vector3U = DirectX::XMUINT3;

using Vector3I = DirectX::XMINT3;

// Color and geometric primitives (from DirectX Collision helper types)
using Color = XMFLOAT4; // RGBA float color

using Sphere = DirectX::BoundingSphere;
using AABB = DirectX::BoundingBox;
using OBB = DirectX::BoundingOrientedBox;
using Frustum = DirectX::BoundingFrustum;
