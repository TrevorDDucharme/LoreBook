#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <GL/glew.h>
#include <CL/cl.h>

namespace Markdown {

class Effect;

// ────────────────────────────────────────────────────────────────────
// ComposedShaderSources — generated GLSL sources for all active stages
// ────────────────────────────────────────────────────────────────────

struct ComposedShaderSources {
    std::string vertex;
    std::string fragment;
    std::string geometry;       // Empty if no geometry stage
    std::string tessControl;    // Empty if no tessellation
    std::string tessEval;       // Empty if no tessellation

    bool hasGeometry() const { return !geometry.empty(); }
    bool hasTessellation() const { return !tessControl.empty() && !tessEval.empty(); }
};

// ────────────────────────────────────────────────────────────────────
// ComposedKernelSource — generated OpenCL kernel source
// ────────────────────────────────────────────────────────────────────

struct ComposedKernelSource {
    std::string source;           // Full OpenCL source text
    std::string entryPoint;       // Kernel function name
    int totalArgCount = 7;        // 7 standard + effect-specific

    // Per-effect arg offsets for bindKernelSnippetParams
    // Maps behaviorID → first arg index for that effect's params
    std::unordered_map<uint32_t, int> effectArgOffsets;
};

// ────────────────────────────────────────────────────────────────────
// ShaderCompositor — assembles snippet fragments into complete shaders
// ────────────────────────────────────────────────────────────────────

class ShaderCompositor {
public:
    ShaderCompositor() = default;

    // ── Glyph shader composition ──
    // Composes glyph snippets from all effects in the stack.
    // Vertex snippets apply outer→inner; fragment snippets apply inner→outer.
    ComposedShaderSources composeGlyphShader(const std::vector<Effect*>& stack);

    // ── Particle shader composition ──
    // Composes particle snippets from all effects in the stack.
    ComposedShaderSources composeParticleShader(const std::vector<Effect*>& stack);

    // ── Kernel composition ──
    // Composes OpenCL kernel snippets from all effects in the stack.
    // Each effect's behavior code runs under its own behaviorID check.
    ComposedKernelSource composeKernel(const std::vector<Effect*>& stack);

    // ── Post-process pass composition ──
    // Generates a complete fragment shader from a PostProcessSnippet.
    std::string composePostProcessFrag(const class PostProcessSnippet& snippet);

    // ── Shader program compilation ──
    // Compiles composed sources into a GL program.
    // Handles optional geometry/tessellation stages.
    static GLuint compileProgram(const std::string& name,
                                 const ComposedShaderSources& sources);

    // Compile an OpenCL program from composed kernel source.
    // commonCL: contents of common.cl to inject.
    static bool compileKernelProgram(const ComposedKernelSource& composed,
                                     const std::string& commonCL,
                                     cl_context ctx, cl_device_id dev,
                                     cl_program& outProgram,
                                     cl_kernel& outKernel);
};

} // namespace Markdown
