#ifndef PATH_TRACER_RNG_HLSLI
#define PATH_TRACER_RNG_HLSLI

// ─── PCG Random Number Generator ─────────────────────────────────────────────
// From: https://www.pcg-random.org/
// Provides a fast stateful RNG suitable for use inside path-tracer wavefronts.
// Only compiled when PATH_TRACER_MODE is active (compute shaders, not rasterized passes).

struct RNG
{
    uint state;
};

uint PCGHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

RNG InitRNG(uint2 pixel, uint accumIndex)
{
    RNG rng;
    uint seed    = pixel.x + pixel.y * 65536u + accumIndex * 6700417u;
    rng.state    = PCGHash(seed);
    return rng;
}

float NextFloat(inout RNG rng)
{
    rng.state = PCGHash(rng.state);
    return float(rng.state) * (1.0f / 4294967296.0f);
}

float2 NextFloat2(inout RNG rng)
{
    return float2(NextFloat(rng), NextFloat(rng));
}

#endif // PATH_TRACER_RNG_HLSLI
