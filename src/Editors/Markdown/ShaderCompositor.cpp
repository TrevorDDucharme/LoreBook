#include <Editors/Markdown/ShaderCompositor.hpp>
#include <Editors/Markdown/Effect.hpp>
#include <plog/Log.h>
#include <sstream>
#include <set>

namespace Markdown {

// Helper: emit uniform declarations, skipping lines already in baseUniforms set
static void emitUniforms(std::ostream& out, const std::string& decls,
                         std::set<std::string>& seen) {
    if (decls.empty()) return;
    std::istringstream iss(decls);
    std::string line;
    while (std::getline(iss, line)) {
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r'))
            line.pop_back();
        if (line.empty()) continue;
        if (seen.insert(line).second)
            out << line << "\n";
    }
}

// ════════════════════════════════════════════════════════════════════
// GLYPH SHADER COMPOSITION
// ════════════════════════════════════════════════════════════════════

ComposedShaderSources ShaderCompositor::composeGlyphShader(const std::vector<Effect*>& stack) {
    ComposedShaderSources out;

    // Collect snippets
    std::vector<GlyphSnippets> snippets;
    for (auto* fx : stack) {
        if (fx) snippets.push_back(fx->getGlyphSnippets());
    }

    // ── Vertex shader ──
    {
        std::ostringstream vs;
        vs << "#version 330 core\n"
           << "layout(location = 0) in vec3 in_pos;\n"
           << "layout(location = 1) in vec2 in_uv;\n"
           << "layout(location = 2) in vec4 in_color;\n"
           << "layout(location = 3) in uint in_effectID;\n\n"
           << "uniform mat4 uMVP;\n"
           << "uniform float uTime;\n";

        // Uniform declarations from all snippets
        for (const auto& s : snippets) {
            if (!s.vertex.uniformDecls.empty()) vs << s.vertex.uniformDecls;
            // Include fragment uniforms too so composite shaders can share
            if (!s.fragment.uniformDecls.empty()) vs << s.fragment.uniformDecls;
        }

        vs << "\nout vec2 v_uv;\n"
           << "out vec4 v_color;\n"
           << "out vec3 v_worldPos;\n"
           << "flat out uint v_effectID;\n";

        // Extra varying declarations
        for (const auto& s : snippets) {
            if (!s.vertex.varyingDecls.empty()) vs << s.vertex.varyingDecls;
        }

        vs << "\n";

        // Helper functions
        for (const auto& s : snippets) {
            if (s.vertex.hasCode() && !s.vertex.helpers.empty())
                vs << s.vertex.helpers << "\n";
        }

        vs << "void main() {\n"
           << "    vec3 pos = in_pos;\n";

        // Vertex snippets: outer→inner order
        for (const auto& s : snippets) {
            if (s.vertex.hasCode())
                vs << "    " << s.vertex.code << "\n";
        }

        vs << "    gl_Position = uMVP * vec4(pos, 1.0);\n"
           << "    v_uv = in_uv;\n"
           << "    v_color = in_color;\n"
           << "    v_worldPos = in_pos;\n"
           << "    v_effectID = in_effectID;\n"
           << "}\n";

        out.vertex = vs.str();
    }

    // ── Fragment shader ──
    {
        std::ostringstream fs;
        fs << "#version 330 core\n"
           << "uniform sampler2D uFontAtlas;\n"
           << "uniform float uTime;\n";

        for (const auto& s : snippets) {
            if (!s.vertex.uniformDecls.empty()) fs << s.vertex.uniformDecls;
            if (!s.fragment.uniformDecls.empty()) fs << s.fragment.uniformDecls;
        }

        fs << "\nin vec2 v_uv;\n"
           << "in vec4 v_color;\n"
           << "in vec3 v_worldPos;\n"
           << "flat in uint v_effectID;\n";

        for (const auto& s : snippets) {
            if (!s.fragment.varyingDecls.empty()) fs << s.fragment.varyingDecls;
        }

        fs << "\nout vec4 fragColor;\n\n";

        for (const auto& s : snippets) {
            if (s.fragment.hasCode() && !s.fragment.helpers.empty())
                fs << s.fragment.helpers << "\n";
        }

        fs << "void main() {\n"
           << "    float alpha = texture(uFontAtlas, v_uv).a;\n"
           << "    vec4 color = vec4(v_color.rgb, v_color.a * alpha);\n";

        // Fragment snippets: inner→outer (reverse order)
        for (int i = static_cast<int>(snippets.size()) - 1; i >= 0; --i) {
            if (snippets[i].fragment.hasCode())
                fs << "    " << snippets[i].fragment.code << "\n";
        }

        fs << "    fragColor = color;\n"
           << "}\n";

        out.fragment = fs.str();
    }

    // ── Geometry shader (optional) ──
    {
        bool needsGeom = false;
        for (const auto& s : snippets)
            if (s.hasGeometry()) { needsGeom = true; break; }

        if (needsGeom) {
            std::ostringstream gs;
            gs << "#version 330 core\n"
               << "layout(triangles) in;\n"
               << "layout(triangle_strip, max_vertices = 3) out;\n\n"
               << "in vec2 v_uv[];\n"
               << "in vec4 v_color[];\n"
               << "in vec3 v_worldPos[];\n"
               << "flat in uint v_effectID[];\n\n"
               << "out vec2 g_uv;\n"
               << "out vec4 g_color;\n"
               << "out vec3 g_worldPos;\n"
               << "flat out uint g_effectID;\n\n"
               << "uniform float uTime;\n";

            for (const auto& s : snippets) {
                if (!s.geometry.uniformDecls.empty()) gs << s.geometry.uniformDecls;
            }
            gs << "\n";

            for (const auto& s : snippets) {
                if (!s.geometry.helpers.empty()) gs << s.geometry.helpers << "\n";
            }

            gs << "void main() {\n"
               << "    for (int i = 0; i < 3; ++i) {\n"
               << "        gl_Position = gl_in[i].gl_Position;\n"
               << "        g_uv = v_uv[i];\n"
               << "        g_color = v_color[i];\n"
               << "        g_worldPos = v_worldPos[i];\n"
               << "        g_effectID = v_effectID[i];\n";

            for (const auto& s : snippets) {
                if (s.geometry.hasCode())
                    gs << "        " << s.geometry.code << "\n";
            }

            gs << "        EmitVertex();\n"
               << "    }\n"
               << "    EndPrimitive();\n"
               << "}\n";

            out.geometry = gs.str();
        }
    }

    // ── Tessellation Control Shader (optional) ──
    {
        bool needsTess = false;
        for (const auto& s : snippets)
            if (s.hasTessellation()) { needsTess = true; break; }

        if (needsTess) {
            std::ostringstream tcs;
            tcs << "#version 400 core\n"
                << "layout(vertices = 3) out;\n\n"
                << "in vec2 v_uv[];\n"
                << "in vec4 v_color[];\n"
                << "in vec3 v_worldPos[];\n"
                << "flat in uint v_effectID[];\n\n"
                << "out vec2 tc_uv[];\n"
                << "out vec4 tc_color[];\n"
                << "out vec3 tc_worldPos[];\n\n"
                << "uniform float uTime;\n";

            for (const auto& s : snippets) {
                if (!s.tessControl.uniformDecls.empty()) tcs << s.tessControl.uniformDecls;
            }
            tcs << "\n";

            for (const auto& s : snippets) {
                if (!s.tessControl.helpers.empty()) tcs << s.tessControl.helpers << "\n";
            }

            tcs << "void main() {\n"
                << "    tc_uv[gl_InvocationID] = v_uv[gl_InvocationID];\n"
                << "    tc_color[gl_InvocationID] = v_color[gl_InvocationID];\n"
                << "    tc_worldPos[gl_InvocationID] = v_worldPos[gl_InvocationID];\n"
                << "    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n\n"
                << "    float innerLevel = 1.0;\n"
                << "    float outerLevel = 1.0;\n";

            for (const auto& s : snippets) {
                if (s.tessControl.hasCode())
                    tcs << "    " << s.tessControl.code << "\n";
            }

            tcs << "    if (gl_InvocationID == 0) {\n"
                << "        gl_TessLevelInner[0] = innerLevel;\n"
                << "        gl_TessLevelOuter[0] = outerLevel;\n"
                << "        gl_TessLevelOuter[1] = outerLevel;\n"
                << "        gl_TessLevelOuter[2] = outerLevel;\n"
                << "    }\n"
                << "}\n";

            out.tessControl = tcs.str();

            // ── Tessellation Evaluation Shader ──
            std::ostringstream tes;
            tes << "#version 400 core\n"
                << "layout(triangles, equal_spacing, ccw) in;\n\n"
                << "in vec2 tc_uv[];\n"
                << "in vec4 tc_color[];\n"
                << "in vec3 tc_worldPos[];\n\n"
                << "out vec2 v_uv;\n"
                << "out vec4 v_color;\n"
                << "out vec3 v_worldPos;\n\n"
                << "uniform mat4 uMVP;\n"
                << "uniform float uTime;\n";

            for (const auto& s : snippets) {
                if (!s.tessEval.uniformDecls.empty()) tes << s.tessEval.uniformDecls;
            }
            tes << "\n";

            for (const auto& s : snippets) {
                if (!s.tessEval.helpers.empty()) tes << s.tessEval.helpers << "\n";
            }

            tes << "void main() {\n"
                << "    vec3 b = gl_TessCoord;\n"
                << "    v_uv = b.x * tc_uv[0] + b.y * tc_uv[1] + b.z * tc_uv[2];\n"
                << "    v_color = b.x * tc_color[0] + b.y * tc_color[1] + b.z * tc_color[2];\n"
                << "    v_worldPos = b.x * tc_worldPos[0] + b.y * tc_worldPos[1] + b.z * tc_worldPos[2];\n"
                << "    vec4 pos = b.x * gl_in[0].gl_Position + b.y * gl_in[1].gl_Position + b.z * gl_in[2].gl_Position;\n"
                << "    vec3 displacement = vec3(0.0);\n";

            for (const auto& s : snippets) {
                if (s.tessEval.hasCode())
                    tes << "    " << s.tessEval.code << "\n";
            }

            tes << "    pos.xyz += displacement;\n"
                << "    gl_Position = pos;\n"
                << "}\n";

            out.tessEval = tes.str();
        }
    }

    return out;
}

// ════════════════════════════════════════════════════════════════════
// PARTICLE SHADER COMPOSITION
// ════════════════════════════════════════════════════════════════════

ComposedShaderSources ShaderCompositor::composeParticleShader(const std::vector<Effect*>& stack) {
    ComposedShaderSources out;

    std::vector<ParticleSnippets> snippets;
    for (auto* fx : stack) {
        if (fx) {
            auto ps = fx->getParticleSnippets();
            if (!ps.empty()) snippets.push_back(std::move(ps));
        }
    }

    // If no snippets, return empty — caller should use fallback
    if (snippets.empty()) return out;

    // ── Vertex shader (pass-through, all effects identical) ──
    {
        std::ostringstream vs;
        vs << "#version 330 core\n"
           << "layout(location = 0) in vec2 in_pos;\n"
           << "layout(location = 1) in float in_z;\n"
           << "layout(location = 2) in vec4 in_color;\n"
           << "layout(location = 3) in float in_size;\n"
           << "layout(location = 4) in float in_life;\n\n"
           << "out float v_z;\n"
           << "out vec4 v_color;\n"
           << "out float v_size;\n"
           << "out float v_life;\n";

        for (const auto& s : snippets) {
            if (!s.vertex.uniformDecls.empty()) vs << s.vertex.uniformDecls;
            if (!s.vertex.varyingDecls.empty()) vs << s.vertex.varyingDecls;
        }

        vs << "\n";
        for (const auto& s : snippets) {
            if (!s.vertex.helpers.empty()) vs << s.vertex.helpers << "\n";
        }

        vs << "void main() {\n"
           << "    gl_Position = vec4(in_pos, 0.0, 1.0);\n"
           << "    v_z = in_z;\n"
           << "    v_color = in_color;\n"
           << "    v_size = in_size;\n"
           << "    v_life = in_life;\n";

        for (const auto& s : snippets) {
            if (s.vertex.hasCode())
                vs << "    " << s.vertex.code << "\n";
        }

        vs << "}\n";
        out.vertex = vs.str();
    }

    // ── Geometry shader (point → quad with snippet modifications) ──
    {
        std::ostringstream gs;
        gs << "#version 330 core\n"
           << "layout(points) in;\n"
           << "layout(triangle_strip, max_vertices = 4) out;\n\n"
           << "uniform mat4 uMVP;\n"
           << "uniform float uTime;\n";

        {
            std::set<std::string> seenUniforms = {"uniform mat4 uMVP;", "uniform float uTime;"};
            for (const auto& s : snippets) {
                emitUniforms(gs, s.geometry.uniformDecls, seenUniforms);
            }
        }

        gs << "\nin float v_z[];\n"
           << "in vec4 v_color[];\n"
           << "in float v_size[];\n"
           << "in float v_life[];\n\n"
           << "out vec2 g_uv;\n"
           << "out vec4 g_color;\n";

        // Extra varyings from geometry snippets (e.g. g_twinkle for sparkle)
        for (const auto& s : snippets) {
            if (!s.geometry.varyingDecls.empty()) gs << s.geometry.varyingDecls;
        }

        gs << "\n";
        for (const auto& s : snippets) {
            if (!s.geometry.helpers.empty()) gs << s.geometry.helpers << "\n";
        }

        gs << "void main() {\n"
           << "    float life = v_life[0];\n"
           << "    if (life <= 0.0) return;\n\n"
           << "    vec3 pos = vec3(gl_in[0].gl_Position.xy, v_z[0]);\n"
           << "    float size = v_size[0];\n"
           << "    vec4 color = v_color[0];\n"
           << "    vec3 right = vec3(1, 0, 0);\n"
           << "    vec3 up = vec3(0, 1, 0);\n\n"
           << "    // Default alpha fade\n"
           << "    color.a *= smoothstep(0.0, 0.3, life);\n\n";

        // Geometry snippet code modifies color, size, life, pos, right, up
        for (const auto& s : snippets) {
            if (s.geometry.hasCode())
                gs << "    " << s.geometry.code << "\n\n";
        }

        // Apply size to right/up after snippet modifications
        gs << "    right *= size;\n"
           << "    up *= size;\n\n"
           // Quad emission (fixed boilerplate)
           << "    g_color = color;\n\n"
           << "    g_uv = vec2(0, 0);\n"
           << "    gl_Position = uMVP * vec4(pos - right - up, 1.0);\n"
           << "    EmitVertex();\n\n"
           << "    g_uv = vec2(1, 0);\n"
           << "    gl_Position = uMVP * vec4(pos + right - up, 1.0);\n"
           << "    EmitVertex();\n\n"
           << "    g_uv = vec2(0, 1);\n"
           << "    gl_Position = uMVP * vec4(pos - right + up, 1.0);\n"
           << "    EmitVertex();\n\n"
           << "    g_uv = vec2(1, 1);\n"
           << "    gl_Position = uMVP * vec4(pos + right + up, 1.0);\n"
           << "    EmitVertex();\n\n"
           << "    EndPrimitive();\n"
           << "}\n";

        out.geometry = gs.str();
    }

    // ── Fragment shader ──
    {
        std::ostringstream fs;
        fs << "#version 330 core\n"
           << "in vec2 g_uv;\n"
           << "in vec4 g_color;\n";

        // Extra varyings received from geometry
        for (const auto& s : snippets) {
            if (!s.fragment.varyingDecls.empty()) fs << s.fragment.varyingDecls;
        }

        fs << "\nuniform float uTime;\n";
        {
            std::set<std::string> seenUniforms = {"uniform float uTime;"};
            for (const auto& s : snippets) {
                emitUniforms(fs, s.fragment.uniformDecls, seenUniforms);
            }
        }

        fs << "\nout vec4 fragColor;\n\n";

        for (const auto& s : snippets) {
            if (!s.fragment.helpers.empty()) fs << s.fragment.helpers << "\n";
        }

        fs << "void main() {\n"
           << "    vec2 uv = g_uv;\n"
           << "    float dist = length(uv - vec2(0.5));\n"
           << "    float alpha = 1.0 - smoothstep(0.3, 0.5, dist);\n"
           << "    vec4 color = g_color;\n\n";

        // Fragment snippets modify alpha and/or color
        // Apply inner→outer (reverse) for consistency with glyph
        for (int i = static_cast<int>(snippets.size()) - 1; i >= 0; --i) {
            if (snippets[i].fragment.hasCode())
                fs << "    " << snippets[i].fragment.code << "\n\n";
        }

        fs << "    fragColor = vec4(color.rgb * alpha, color.a * alpha);\n"
           << "}\n";

        out.fragment = fs.str();
    }

    // ── Tessellation (optional) ──
    {
        bool needsTess = false;
        for (const auto& s : snippets)
            if (s.hasTessellation()) { needsTess = true; break; }

        if (needsTess) {
            // TCS
            std::ostringstream tcs;
            tcs << "#version 400 core\n"
                << "layout(vertices = 1) out;\n\n"
                << "in float v_z[];\n"
                << "in vec4 v_color[];\n"
                << "in float v_size[];\n"
                << "in float v_life[];\n\n"
                << "out float tc_z[];\n"
                << "out vec4 tc_color[];\n"
                << "out float tc_size[];\n"
                << "out float tc_life[];\n\n"
                << "uniform float uTime;\n";

            for (const auto& s : snippets) {
                if (!s.tessControl.uniformDecls.empty()) tcs << s.tessControl.uniformDecls;
            }

            tcs << "\nvoid main() {\n"
                << "    tc_z[gl_InvocationID] = v_z[gl_InvocationID];\n"
                << "    tc_color[gl_InvocationID] = v_color[gl_InvocationID];\n"
                << "    tc_size[gl_InvocationID] = v_size[gl_InvocationID];\n"
                << "    tc_life[gl_InvocationID] = v_life[gl_InvocationID];\n"
                << "    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n\n"
                << "    float innerLevel = 1.0;\n"
                << "    float outerLevel = 1.0;\n";

            for (const auto& s : snippets) {
                if (s.tessControl.hasCode())
                    tcs << "    " << s.tessControl.code << "\n";
            }

            tcs << "    gl_TessLevelInner[0] = innerLevel;\n"
                << "    gl_TessLevelOuter[0] = outerLevel;\n"
                << "    gl_TessLevelOuter[1] = outerLevel;\n"
                << "}\n";

            out.tessControl = tcs.str();

            // TES
            std::ostringstream tes;
            tes << "#version 400 core\n"
                << "layout(isolines, equal_spacing) in;\n\n"
                << "in float tc_z[];\n"
                << "in vec4 tc_color[];\n"
                << "in float tc_size[];\n"
                << "in float tc_life[];\n\n"
                << "out float v_z;\n"
                << "out vec4 v_color;\n"
                << "out float v_size;\n"
                << "out float v_life;\n\n"
                << "uniform float uTime;\n";

            for (const auto& s : snippets) {
                if (!s.tessEval.uniformDecls.empty()) tes << s.tessEval.uniformDecls;
            }

            tes << "\nvoid main() {\n"
                << "    float t = gl_TessCoord.x;\n"
                << "    v_z = tc_z[0];\n"
                << "    v_color = tc_color[0];\n"
                << "    v_size = tc_size[0];\n"
                << "    v_life = tc_life[0];\n"
                << "    gl_Position = gl_in[0].gl_Position;\n";

            for (const auto& s : snippets) {
                if (s.tessEval.hasCode())
                    tes << "    " << s.tessEval.code << "\n";
            }

            tes << "}\n";
            out.tessEval = tes.str();
        }
    }

    return out;
}

// ════════════════════════════════════════════════════════════════════
// KERNEL COMPOSITION
// ════════════════════════════════════════════════════════════════════

ComposedKernelSource ShaderCompositor::composeKernel(const std::vector<Effect*>& stack) {
    ComposedKernelSource out;

    struct KernelInfo {
        Effect* effect;
        KernelSnippet snippet;
        uint32_t behaviorID;
    };

    std::vector<KernelInfo> infos;
    for (auto* fx : stack) {
        if (!fx) continue;
        auto ks = fx->getKernelSnippet();
        if (ks.empty()) continue;
        infos.push_back({fx, std::move(ks), fx->getBehaviorID()});
    }

    if (infos.empty()) return out;

    // Build entry point name
    if (infos.size() == 1) {
        out.entryPoint = "updateEffect_" + std::to_string(infos[0].behaviorID);
    } else {
        out.entryPoint = "updateComposite";
        for (auto& i : infos)
            out.entryPoint += "_" + std::to_string(i.behaviorID);
    }

    std::ostringstream src;
    src << "// Auto-composed kernel: " << out.entryPoint << "\n"
        << "#include \"common.cl\"\n\n";

    // Collect all helpers
    for (auto& info : infos) {
        if (!info.snippet.helpers.empty())
            src << info.snippet.helpers << "\n";
    }

    // Build kernel signature with merged args
    src << "__kernel void " << out.entryPoint << "(\n"
        << "    __global Particle* particles,\n"
        << "    __read_only image2d_t collision,\n"
        << "    const float deltaTime,\n"
        << "    const float scrollY,\n"
        << "    const float maskHeight,\n"
        << "    const float time,\n"
        << "    const uint count,\n"
        << "    const float maskScale";

    // Append each effect's extra args
    int argIdx = 8;
    for (auto& info : infos) {
        out.effectArgOffsets[info.behaviorID] = argIdx;
        if (!info.snippet.argDecls.empty()) {
            src << info.snippet.argDecls;
            // Count args by counting commas in argDecls
            int commaCount = 0;
            for (char c : info.snippet.argDecls)
                if (c == ',') commaCount++;
            argIdx += commaCount;
        }
    }

    out.totalArgCount = argIdx;

    src << "\n) {\n"
        << "    uint gid = get_global_id(0);\n"
        << "    if (gid >= count) return;\n\n"
        << "    Particle p = particles[gid];\n"
        << "    if (p.life <= 0.0f) return;\n\n"
        << "    uint rngState = (uint)(p.pos.x * 1000.0f) ^ (uint)(p.pos.y * 1000.0f) ^ gid;\n\n";

    // Each effect's behavior code under its behaviorID check
    for (size_t i = 0; i < infos.size(); ++i) {
        auto& info = infos[i];
        src << "    // ── Effect: behaviorID=" << info.behaviorID << " ──\n";
        if (infos.size() > 1) {
            src << "    if (p.behaviorID == " << info.behaviorID << "u) {\n";
        } else {
            src << "    if (p.behaviorID == " << info.behaviorID << "u) {\n";
        }

        // Behavior code — sets up physics, velocity, life decay, color, etc.
        src << "    " << info.snippet.behaviorCode << "\n\n";

        // Default position update
        src << "    float2 newPos = p.pos + p.vel * deltaTime;\n\n";

        // Collision check
        src << "    // Collision detection against glyph collision mask\n"
            << "    float2 newMaskPos = docToMask(newPos, scrollY, maskHeight, maskScale);\n"
            << "    float2 curMaskPos = docToMask(p.pos, scrollY, maskHeight, maskScale);\n"
            << "    float newCol = sampleCollision(collision, collisionSampler, newMaskPos);\n"
            << "    float curCol = sampleCollision(collision, collisionSampler, curMaskPos);\n\n"
            // Case 1: entering solid from free-space (standard collision)
            // Case 2: new position in solid even if current is too
            //         (skip for freshly spawned particles still inside birth glyph)
            << "    float lifeElapsed = p.maxLife - p.life;\n"
            << "    bool freshlySpawned = (lifeElapsed < 0.15f) && (curCol > 0.5f);\n"
            << "    if (newCol > 0.5f && !freshlySpawned) {\n"
            << "        float2 maskNorm = surfaceNormal(collision, collisionSampler, newMaskPos);\n"
            << "        float2 docNorm = (float2)(maskNorm.x, -maskNorm.y);\n";

        if (!info.snippet.collisionResponse.empty()) {
            src << "        " << info.snippet.collisionResponse << "\n";
        } else {
            // Default bounce
            src << "        p.vel = reflect_f2(p.vel, docNorm) * "
                << info.snippet.defaultDamping << "f;\n"
                << "        newPos = p.pos + p.vel * deltaTime;\n";
        }

        src << "    }\n\n"
            << "    p.pos = newPos;\n"
            << "    } // end behaviorID=" << info.behaviorID << "\n\n";
    }

    src << "    particles[gid] = p;\n"
        << "}\n";

    out.source = src.str();
    return out;
}

// ════════════════════════════════════════════════════════════════════
// POST-PROCESS COMPOSITION
// ════════════════════════════════════════════════════════════════════

std::string ShaderCompositor::composePostProcessFrag(const PostProcessSnippet& snippet) {
    std::ostringstream fs;
    fs << "#version 330 core\n"
       << "uniform sampler2D uInputTex;\n"
       << "uniform vec2 uResolution;\n"
       << "uniform float uTime;\n";

    if (!snippet.fragment.uniformDecls.empty())
        fs << snippet.fragment.uniformDecls;

    fs << "\nin vec2 v_uv;\n"
       << "out vec4 fragColor;\n\n";

    if (!snippet.fragment.helpers.empty())
        fs << snippet.fragment.helpers << "\n";

    fs << "void main() {\n"
       << "    vec2 uv = v_uv;\n"
       << "    vec4 color = texture(uInputTex, uv);\n\n";

    if (snippet.fragment.hasCode())
        fs << "    " << snippet.fragment.code << "\n\n";

    fs << "    fragColor = color;\n"
       << "}\n";

    return fs.str();
}

// ════════════════════════════════════════════════════════════════════
// SHADER PROGRAM COMPILATION
// ════════════════════════════════════════════════════════════════════

GLuint ShaderCompositor::compileProgram(const std::string& name,
                                         const ComposedShaderSources& sources) {
    if (sources.vertex.empty() || sources.fragment.empty()) {
        PLOG_ERROR << "Cannot compile program '" << name << "': missing vertex or fragment source";
        return 0;
    }

    auto compileStage = [&](GLenum type, const std::string& src, const char* stageName) -> GLuint {
        GLuint shader = glCreateShader(type);
        const char* srcPtr = src.c_str();
        glShaderSource(shader, 1, &srcPtr, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[1024];
            glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
            PLOG_ERROR << "Shader compilation failed (" << name << " " << stageName << "): " << infoLog;
            PLOG_ERROR << "Source:\n" << src;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vert = compileStage(GL_VERTEX_SHADER, sources.vertex, "vertex");
    if (!vert) return 0;

    GLuint frag = compileStage(GL_FRAGMENT_SHADER, sources.fragment, "fragment");
    if (!frag) { glDeleteShader(vert); return 0; }

    GLuint geom = 0;
    if (sources.hasGeometry()) {
        geom = compileStage(GL_GEOMETRY_SHADER, sources.geometry, "geometry");
        if (!geom) { glDeleteShader(vert); glDeleteShader(frag); return 0; }
    }

    GLuint tcs = 0, tes = 0;
    if (sources.hasTessellation()) {
        tcs = compileStage(GL_TESS_CONTROL_SHADER, sources.tessControl, "tess_control");
        if (!tcs) { glDeleteShader(vert); glDeleteShader(frag); if (geom) glDeleteShader(geom); return 0; }

        tes = compileStage(GL_TESS_EVALUATION_SHADER, sources.tessEval, "tess_eval");
        if (!tes) { glDeleteShader(vert); glDeleteShader(frag); if (geom) glDeleteShader(geom); glDeleteShader(tcs); return 0; }
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    if (geom) glAttachShader(program, geom);
    if (tcs) glAttachShader(program, tcs);
    if (tes) glAttachShader(program, tes);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        PLOG_ERROR << "Shader program link failed (" << name << "): " << infoLog;
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    if (geom) glDeleteShader(geom);
    if (tcs) glDeleteShader(tcs);
    if (tes) glDeleteShader(tes);

    if (program)
        PLOG_DEBUG << "Compiled shader program: " << name;

    return program;
}

bool ShaderCompositor::compileKernelProgram(const ComposedKernelSource& composed,
                                             const std::string& commonCL,
                                             cl_context ctx, cl_device_id dev,
                                             cl_program& outProgram,
                                             cl_kernel& outKernel) {
    outProgram = nullptr;
    outKernel = nullptr;

    std::string fullSource = composed.source;
    size_t includePos = fullSource.find("#include \"common.cl\"");
    if (includePos != std::string::npos) {
        fullSource.replace(includePos, 20, commonCL);
    }

    const char* src = fullSource.c_str();
    size_t len = fullSource.size();
    cl_int err;

    outProgram = clCreateProgramWithSource(ctx, 1, &src, &len, &err);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "clCreateProgramWithSource failed for " << composed.entryPoint << ": " << err;
        return false;
    }

    PLOG_DEBUG << "Kernel source (" << fullSource.size() << " bytes), common.cl injected: " 
               << (includePos != std::string::npos ? "yes" : "no") 
               << ", commonCL size: " << commonCL.size();
    
    err = clBuildProgram(outProgram, 1, &dev, "-cl-fast-relaxed-math", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize;
        clGetProgramBuildInfo(outProgram, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::string log(logSize, '\0');
        clGetProgramBuildInfo(outProgram, dev, CL_PROGRAM_BUILD_LOG, logSize, &log[0], nullptr);
        PLOG_ERROR << "Kernel build failed for " << composed.entryPoint << ": " << log;
        clReleaseProgram(outProgram);
        outProgram = nullptr;
        return false;
    }

    outKernel = clCreateKernel(outProgram, composed.entryPoint.c_str(), &err);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "clCreateKernel failed for " << composed.entryPoint << ": " << err;
        clReleaseProgram(outProgram);
        outProgram = nullptr;
        return false;
    }

    PLOG_DEBUG << "Compiled kernel: " << composed.entryPoint
               << " (" << composed.totalArgCount << " args)";
    return true;
}

} // namespace Markdown
