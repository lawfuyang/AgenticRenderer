# Build Pipeline

**SRRHI**

* Trigger: `.sr` added/modified
* Run: `build_srrhi`
* Outputs:

  * C++ → `src/shaders/srrhi/cpp`
  * HLSL → `src/shaders/srrhi/hlsl`
* ❗ Never edit generated files
* Include:

  * C++ → from `cpp`
  * Shaders → from `hlsl`
* Ref: `external/srrhi/README.md`
* Order: **must run before C++ + shader compilation**

**Shaders**

* Cmd: `build_shaders` (CMake)
* Depends on: `build_srrhi` (auto)

---

# Coding Conventions

**Types**: `UpperCamelCase` (nouns)
→ `Raster`, `ImageSprite`

**Functions**: `UpperCamelCase` (nouns)

**bool**: `b` prefix
→ `bIsCorrect`

**Local vars**: `lowerCamelCase`
→ `i`, `myWidth`

**Members**: `m_UpperCamelCase`
→ `m_Width`, `m_bIsTrue`

**Constants**:

* `SCREAMING_SNAKE_CASE` OR
* `kLowerCamelCase`
  → `MAX_LOD_COUNT`, `kThreadsPerGroup`

**Globals**: `g_UpperCamelCase`
**Static globals**: `gs_UpperCamelCase`
