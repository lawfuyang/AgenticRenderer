# Build Pipeline Rules

## SRRHI Generation
Trigger condition:
- Any `.sr` file is added or modified

Required action:
- Run: `build_srrhi`

Outputs:
- C++ headers: `src/shaders/srrhi/cpp`
- HLSL headers: `src/shaders/srrhi/hlsl`
- NEVER edit these files manually

Integration requirements:
- C++ source files must `#include` generated headers from `cpp`
- Shader source files must `#include` generated headers from `hlsl`

Reference:
- See: `external/srrhi/README.md`

Ordering constraint:
- `build_srrhi` MUST run before:
  - C++ compilation
  - Shader compilation


## Shader Compilation
Command:
- `build_shaders` (CMake target)

Dependency:
- Must run AFTER `build_srrhi`
- Enforced via CMake dependency (automatic)


# Coding Conventions

## Naming

### Types (Classes, Structs)
- Format: `UpperCamelCase`
- Use nouns
- Examples:
  - `Raster`
  - `ImageSprite`

### Functions / Methods
- Format: `UpperCamelCase`
- Use nouns

### Local Variables (stack)
- Format: `lowerCamelCase`
- Examples:
  - `i`
  - `c`
  - `myWidth`

### Constants
- Allowed formats:
  - `SCREAMING_SNAKE_CASE`
  - Prefix with `k` (lowerCamelCase after prefix)

- Examples:
  - `MAX_LOD_COUNT`
  - `kThreadsPerGroup`
