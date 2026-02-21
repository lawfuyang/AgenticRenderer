#include "Utilities.h"

float Halton(uint32_t index, uint32_t base)
{
    float result = 0.0f;
    float f = 1.0f / (float)base;
    uint32_t i = index;
    while (i > 0)
    {
        result += f * (float)(i % base);
        i /= base;
        f /= (float)base;
    }
    return result;
}

Vector3 CalculateGridZParams(float NearPlane, float FarPlane, float DepthDistributionScale, uint32_t GridSizeZ)
{
    // S = distribution scale
    // B, O are solved for given the z distances of the first+last slice, and the # of slices.
    //
    // slice = log2(z*B + O) * S

    // Don't spend lots of resolution right in front of the near plane
    float NearOffset = .095 * 100;

    // Space out the slices so they aren't all clustered at the near plane
    float S = DepthDistributionScale;

    float N = NearPlane + NearOffset;
    float F = FarPlane;

    float O = (F - N * exp2(GridSizeZ / S)) / (F - N);
    float B = (1 - O) / N;

    return Vector3{ B, O, S };
}

Vector2 CreateInvDeviceZToWorldZTransform(const Matrix& ProjMatrix)
{
	// The perspective depth projection comes from the the following projection matrix:
	//
	// | 1  0  0  0 |
	// | 0  1  0  0 |
	// | 0  0  A  1 |
	// | 0  0  B  0 |
	//
	// Z' = (Z * A + B) / Z
	// Z' = A + B / Z
	//
	// So to get Z from Z' is just:
	// Z = B / (Z' - A)
	//
	// Note a reversed Z projection matrix will have A=0.
	//
	// Done in shader as:
	// Z = 1 / (Z' * C1 - C2)   --- Where C1 = 1/B, C2 = A/B
	//

	float DepthMul = ProjMatrix.m[2][2];
	float DepthAdd = ProjMatrix.m[3][2];

	if (DepthAdd == 0.f)
	{
		// Avoid dividing by 0 in this case
		DepthAdd = 0.00000001f;
	}

	// SceneDepth = 1.0f / (DeviceZ / ProjMatrix.M[3][2] - ProjMatrix.M[2][2] / ProjMatrix.M[3][2])

	// combined equation in shader to handle either
	// SceneDepth = DeviceZ * View.InvDeviceZToWorldZTransform[0] + View.InvDeviceZToWorldZTransform[1] + 1.0f / (DeviceZ * View.InvDeviceZToWorldZTransform[2] - View.InvDeviceZToWorldZTransform[3]);

	// therefore perspective needs
	// InvDeviceZToWorldZTransform[0] = 1.0f / ProjMatrix.M[3][2]
	// InvDeviceZToWorldZTransform[1] = ProjMatrix.M[2][2] / ProjMatrix.M[3][2]

    float SubtractValue = DepthMul / DepthAdd;

    // Subtract a tiny number to avoid divide by 0 errors in the shader when a very far distance is decided from the depth buffer.
    // This fixes fog not being applied to the black background in the editor.
    SubtractValue -= 0.00000001f;

    return Vector2{ 1.0f / DepthAdd, SubtractValue };
}
