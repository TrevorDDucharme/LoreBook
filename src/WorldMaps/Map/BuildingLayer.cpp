#include <WorldMaps/Map/BuildingLayer.hpp>
#include <WorldMaps/World/World.hpp>
#include <Vault.hpp>
#include <plog/Log.h>
#include <tracy/Tracy.hpp>
#include <sstream>
#include <algorithm>
#include <cmath>

// ── lifecycle ───────────────────────────────────────────────────────────────

BuildingLayer::~BuildingLayer()
{
    if (colorBuffer_) {
        OpenCLContext::get().releaseMem(colorBuffer_);
        colorBuffer_ = nullptr;
    }
}

// ── MapLayer interface ──────────────────────────────────────────────────────

cl_mem BuildingLayer::sample()  { return getColor(); }

cl_mem BuildingLayer::getColor()
{
    ZoneScopedN("BuildingLayer::getColor");

    // If templates haven't been loaded yet and a vault is now available, retry
    if (!templatesLoaded_ && parentWorld && parentWorld->getVault()) {
        dirty_ = true;
    }

    if (dirty_ || colorBuffer_ == nullptr) {
        loadTemplatesAndScatter();
        rasterizeToGPU();
        dirty_ = false;
    }
    return colorBuffer_;
}

// ── parseParameters ─────────────────────────────────────────────────────────

void BuildingLayer::parseParameters(const std::string& params)
{
    auto lock = lockParameters();
    // Expected format: "minDistance:100,maxBuildings:200,seed:42,cellsPerMeter:3"
    auto trim = [](std::string s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
        return s;
    };

    std::istringstream iss(params);
    std::string token;
    while (std::getline(iss, token, ',')) {
        auto pos = token.find(':');
        if (pos == std::string::npos) continue;
        std::string key   = trim(token.substr(0, pos));
        std::string value = trim(token.substr(pos + 1));
        try {
            if      (key == "minDistance")    minDistance_    = std::stof(value);
            else if (key == "maxBuildings")  maxBuildings_  = std::stoi(value);
            else if (key == "seed")          seed_          = static_cast<unsigned int>(std::stoul(value));
            else if (key == "cellsPerMeter") cellsPerMeter_ = std::stof(value);
        } catch (...) {
            PLOGW << "BuildingLayer::parseParameters: bad value for " << key;
        }
    }
    dirty_ = true;
    templatesLoaded_ = false; // force reload on next getColor()
}

// ── setTemplates ────────────────────────────────────────────────────────────

void BuildingLayer::setTemplates(const std::vector<FloorPlan>& t)
{
    templates_       = t;
    templatesLoaded_ = true;
    dirty_           = true;
}

// ── loadTemplatesAndScatter ─────────────────────────────────────────────────

void BuildingLayer::loadTemplatesAndScatter()
{
    ZoneScopedN("BuildingLayer::loadTemplatesAndScatter");

    // Load templates from vault if not already loaded
    if (!templatesLoaded_ && parentWorld) {
        Vault* vault = parentWorld->getVault();
        if (vault) {
            try {
                auto infos = vault->listFloorPlanTemplates();
                PLOGI << "BuildingLayer: vault has " << infos.size() << " template listing(s)";
                templates_.clear();
                for (const auto& info : infos) {
                    std::string json = vault->loadFloorPlanTemplate(info.id);
                    if (!json.empty()) {
                        FloorPlan fp = FloorPlanJSON::deserialize(json);
                        PLOGI << "BuildingLayer: template '" << info.name
                              << "' has " << fp.walls.size() << " walls, "
                              << fp.rooms.size() << " rooms, "
                              << fp.doors.size() << " doors, "
                              << fp.furniture.size() << " furniture";
                        templates_.push_back(std::move(fp));
                    } else {
                        PLOGW << "BuildingLayer: template id=" << info.id << " returned empty JSON";
                    }
                }
                PLOGI << "BuildingLayer: loaded " << templates_.size()
                      << " floorplan template(s) from vault";
                templatesLoaded_ = true;  // Only mark loaded when vault was available
            } catch (const std::exception& e) {
                PLOGE << "BuildingLayer: failed to load templates: " << e.what();
                templatesLoaded_ = true;  // Don't retry on error
            }
        } else {
            PLOGD << "BuildingLayer: vault not available yet, will retry";
            // Do NOT set templatesLoaded_ — retry when vault becomes available
        }
    }

    if (templates_.empty()) {
        PLOGW << "BuildingLayer: no floorplan templates available — layer will be empty";
        placedBuildings.clear();
        return;
    }

    // ── Pre-compute template metadata (centroid & half-extent) ──
    struct TemplateMeta {
        float centerX = 0, centerY = 0;
        float halfExtentX = 0, halfExtentY = 0;
    };
    std::vector<TemplateMeta> tmeta(templates_.size());
    float maxHalfExtent = 0.0f;

    for (size_t ti = 0; ti < templates_.size(); ++ti) {
        const auto& fp = templates_[ti];
        float sumX = 0, sumY = 0;
        float fMinX = 1e10f, fMinY = 1e10f, fMaxX = -1e10f, fMaxY = -1e10f;
        int n = 0;

        auto accum = [&](float x, float y) {
            sumX += x; sumY += y; ++n;
            fMinX = std::min(fMinX, x); fMaxX = std::max(fMaxX, x);
            fMinY = std::min(fMinY, y); fMaxY = std::max(fMaxY, y);
        };

        for (const auto& w : fp.walls) {
            accum(w.start.x, w.start.y);
            accum(w.end.x,   w.end.y);
        }
        for (const auto& r : fp.rooms) {
            for (const auto& v : r.vertices) accum(v.x, v.y);
        }

        if (n > 0) {
            tmeta[ti].centerX = sumX / n;
            tmeta[ti].centerY = sumY / n;
        }
        if (fMinX <= fMaxX) {
            tmeta[ti].halfExtentX = std::max(fMaxX - tmeta[ti].centerX,
                                              tmeta[ti].centerX - fMinX);
            tmeta[ti].halfExtentY = std::max(fMaxY - tmeta[ti].centerY,
                                              tmeta[ti].centerY - fMinY);
        }
        maxHalfExtent = std::max(maxHalfExtent,
            std::max(tmeta[ti].halfExtentX, tmeta[ti].halfExtentY));

        PLOGI << "BuildingLayer: template " << ti
              << " center=(" << tmeta[ti].centerX << "," << tmeta[ti].centerY
              << ") halfExtent=(" << tmeta[ti].halfExtentX
              << "," << tmeta[ti].halfExtentY << ")";
    }

    // ── Compute dynamic scatter parameters ──
    int latRes = parentWorld ? parentWorld->getWorldLatitudeResolution() : 4096;
    int lonRes = parentWorld ? parentWorld->getWorldLongitudeResolution() : 4096;

    float buildingRadiusCells = maxHalfExtent * cellsPerMeter_ + 20.0f;
    float edgeMargin = buildingRadiusCells + 10.0f;
    float effectiveMinDist = std::max(minDistance_, buildingRadiusCells * 2.1f);

    if (edgeMargin * 2 >= (float)latRes || edgeMargin * 2 >= (float)lonRes) {
        PLOGW << "BuildingLayer: buildings too large for map at cellsPerMeter="
              << cellsPerMeter_ << " (edgeMargin=" << edgeMargin
              << ", map=" << latRes << "x" << lonRes << ")";
        placedBuildings.clear();
        return;
    }

    std::mt19937 rng(seed_);
    std::uniform_real_distribution<float> distX(edgeMargin, (float)latRes - edgeMargin);
    std::uniform_real_distribution<float> distY(edgeMargin, (float)lonRes - edgeMargin);
    std::uniform_int_distribution<size_t> templateDist(0, templates_.size() - 1);

    placedBuildings.clear();
    int maxAttempts = maxBuildings_ * 20;

    for (int attempt = 0;
         attempt < maxAttempts && (int)placedBuildings.size() < maxBuildings_;
         ++attempt)
    {
        float x = distX(rng);
        float y = distY(rng);

        // Reject if too close to an existing building (using scatter centres)
        bool tooClose = false;
        for (const auto& existing : placedBuildings) {
            float dx = x - existing.scatterX;
            float dy = y - existing.scatterY;
            if (dx * dx + dy * dy < effectiveMinDist * effectiveMinDist) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        size_t ti = templateDist(rng);
        const auto& tm = tmeta[ti];

        PlacedBuilding pb;
        pb.scatterX = x;
        pb.scatterY = y;
        // Adjust origin so the floor plan centroid maps to the scatter centre
        pb.mapX = x - tm.centerX * cellsPerMeter_;
        pb.mapY = y - tm.centerY * cellsPerMeter_;
        pb.plan = templates_[ti];
        computeBuildingBounds(pb);

        // Reject if any part of the building extends beyond the map
        if (pb.minX < 0 || pb.minY < 0 ||
            pb.maxX >= (float)latRes || pb.maxY >= (float)lonRes) {
            continue;
        }

        placedBuildings.push_back(std::move(pb));
    }

    PLOGI << "BuildingLayer: placed " << placedBuildings.size() << " building(s)"
          << " (edgeMargin=" << edgeMargin
          << ", effectiveMinDist=" << effectiveMinDist << ")";
}

// ── computeBuildingBounds ───────────────────────────────────────────────────

void BuildingLayer::computeBuildingBounds(PlacedBuilding& b)
{
    float bMinX =  1e10f, bMinY =  1e10f;
    float bMaxX = -1e10f, bMaxY = -1e10f;

    auto expand = [&](float mx, float my) {
        bMinX = std::min(bMinX, mx);
        bMinY = std::min(bMinY, my);
        bMaxX = std::max(bMaxX, mx);
        bMaxY = std::max(bMaxY, my);
    };

    for (const auto& wall : b.plan.walls) {
        auto pts = wall.getSampledPoints();
        for (const auto& p : pts)
            expand(b.mapX + p.x * cellsPerMeter_,
                   b.mapY + p.y * cellsPerMeter_);
    }

    for (const auto& room : b.plan.rooms) {
        for (const auto& v : room.vertices)
            expand(b.mapX + v.x * cellsPerMeter_,
                   b.mapY + v.y * cellsPerMeter_);
    }

    // Fallback if no geometry
    if (bMinX > bMaxX) {
        bMinX = b.mapX - 5.0f;  bMinY = b.mapY - 5.0f;
        bMaxX = b.mapX + 5.0f;  bMaxY = b.mapY + 5.0f;
    }

    b.minX = bMinX;  b.minY = bMinY;
    b.maxX = bMaxX;  b.maxY = bMaxY;
}

// ── helper: convert ImU32 colour to cl_float4 ──────────────────────────────

static cl_float4 imcolToFloat4(ImU32 c)
{
    cl_float4 f;
    f.s[0] = ((c >>  0) & 0xFF) / 255.0f;
    f.s[1] = ((c >>  8) & 0xFF) / 255.0f;
    f.s[2] = ((c >> 16) & 0xFF) / 255.0f;
    f.s[3] = ((c >> 24) & 0xFF) / 255.0f;
    return f;
}

// ── rasterizeToGPU ──────────────────────────────────────────────────────────

void BuildingLayer::rasterizeToGPU()
{
    ZoneScopedN("BuildingLayer::rasterizeToGPU");

    if (!OpenCLContext::get().isReady() || !parentWorld) return;

    int latRes = parentWorld->getWorldLatitudeResolution();
    int lonRes = parentWorld->getWorldLongitudeResolution();
    size_t voxels = (size_t)latRes * (size_t)lonRes;
    size_t outSize = voxels * sizeof(cl_float4);
    cl_int err = CL_SUCCESS;
    cl_command_queue queue = OpenCLContext::get().getQueue();

    // Ensure output buffer exists
    if (colorBuffer_ != nullptr) {
        size_t bufferSize = 0;
        err = clGetMemObjectInfo(colorBuffer_, CL_MEM_SIZE,
                                 sizeof(size_t), &bufferSize, nullptr);
        if (err != CL_SUCCESS || bufferSize < outSize) {
            OpenCLContext::get().releaseMem(colorBuffer_);
            colorBuffer_ = nullptr;
        }
    }
    if (colorBuffer_ == nullptr) {
        colorBuffer_ = OpenCLContext::get().createBuffer(
            CL_MEM_READ_WRITE, outSize, nullptr, &err, "BuildingLayer output");
        if (err != CL_SUCCESS || !colorBuffer_) {
            PLOGE << "BuildingLayer: failed to allocate output buffer";
            return;
        }
    }

    // If nothing to draw, clear to background colour and return
    if (placedBuildings.empty()) {
        cl_float4 bg = {{0.88f, 0.85f, 0.78f, 1.0f}};
        clEnqueueFillBuffer(queue, colorBuffer_, &bg,
                            sizeof(cl_float4), 0, outSize, 0, nullptr, nullptr);
        return;
    }

    // ================================================================
    // 1. Extract room polygons from all placed buildings
    // ================================================================

    struct RoomData {
        std::vector<cl_float2> vertices; // boundary in map coords
        cl_float4 fillColor;
        cl_float4 bounds; // minX, minY, maxX, maxY
    };
    std::vector<RoomData> allRooms;

    for (const auto& pb : placedBuildings) {
        for (const auto& room : pb.plan.rooms) {
            auto boundary = room.getSampledBoundary(10);
            if (boundary.size() < 3) continue;

            RoomData rd;
            rd.fillColor = imcolToFloat4(room.fillColor);

            float rMinX = 1e10f, rMinY = 1e10f, rMaxX = -1e10f, rMaxY = -1e10f;
            for (const auto& pt : boundary) {
                cl_float2 v;
                v.s[0] = pb.mapX + pt.x * cellsPerMeter_;
                v.s[1] = pb.mapY + pt.y * cellsPerMeter_;
                rd.vertices.push_back(v);
                rMinX = std::min(rMinX, v.s[0]);
                rMinY = std::min(rMinY, v.s[1]);
                rMaxX = std::max(rMaxX, v.s[0]);
                rMaxY = std::max(rMaxY, v.s[1]);
            }
            rd.bounds.s[0] = rMinX; rd.bounds.s[1] = rMinY;
            rd.bounds.s[2] = rMaxX; rd.bounds.s[3] = rMaxY;

            allRooms.push_back(std::move(rd));
        }
    }

    // Flatten room data into GPU arrays
    std::vector<cl_float2> flatRoomVerts;
    std::vector<cl_int>    roomVertStart;
    std::vector<cl_int>    roomVertCount;
    std::vector<cl_float4> roomFillColors;
    std::vector<cl_float4> roomBoundsVec;

    for (const auto& rd : allRooms) {
        roomVertStart.push_back(static_cast<cl_int>(flatRoomVerts.size()));
        roomVertCount.push_back(static_cast<cl_int>(rd.vertices.size()));
        roomFillColors.push_back(rd.fillColor);
        roomBoundsVec.push_back(rd.bounds);
        for (const auto& v : rd.vertices)
            flatRoomVerts.push_back(v);
    }
    int totalRoomCount = static_cast<int>(allRooms.size());

    // ================================================================
    // 2. Extract line segments from all placed buildings
    // ================================================================

    struct SegmentData {
        cl_float4 coords;       // x0, y0, x1, y1
        cl_float4 color;
        float     halfThickness;
    };

    // Segments grouped by building for bounding-box culling
    std::vector<std::vector<SegmentData>> buildingSegs(placedBuildings.size());

    for (size_t bi = 0; bi < placedBuildings.size(); ++bi) {
        const auto& pb   = placedBuildings[bi];
        const auto& plan = pb.plan;
        auto& segs       = buildingSegs[bi];

        // ── walls ──
        for (const auto& wall : plan.walls) {
            auto pts = wall.getSampledPoints();
            cl_float4 wc = imcolToFloat4(wall.color);
            float ht = wall.thickness * cellsPerMeter_ * 0.5f;
            if (ht < 0.5f) ht = 0.5f;

            for (size_t i = 1; i < pts.size(); ++i) {
                SegmentData sd;
                sd.coords.s[0] = pb.mapX + pts[i-1].x * cellsPerMeter_;
                sd.coords.s[1] = pb.mapY + pts[i-1].y * cellsPerMeter_;
                sd.coords.s[2] = pb.mapX + pts[i  ].x * cellsPerMeter_;
                sd.coords.s[3] = pb.mapY + pts[i  ].y * cellsPerMeter_;
                sd.color = wc;
                sd.halfThickness = ht;
                segs.push_back(sd);
            }
        }

        // ── doors ──
        for (const auto& door : plan.doors) {
            const Wall* wall = nullptr;
            for (const auto& w : plan.walls)
                if (w.id == door.wallId) { wall = &w; break; }
            if (!wall) continue;

            ImVec2 pos  = wall->pointAt(door.positionOnWall);
            ImVec2 tan  = wall->tangentAt(door.positionOnWall);
            ImVec2 norm = wall->normalAt(door.positionOnWall);
            float  hw   = door.width * 0.5f;
            cl_float4 dc = imcolToFloat4(door.color);
            float ht = wall->thickness * cellsPerMeter_ * 0.5f;
            if (ht < 0.5f) ht = 0.5f;

            // Opening line along tangent
            {
                SegmentData sd;
                sd.coords.s[0] = pb.mapX + (pos.x - tan.x * hw) * cellsPerMeter_;
                sd.coords.s[1] = pb.mapY + (pos.y - tan.y * hw) * cellsPerMeter_;
                sd.coords.s[2] = pb.mapX + (pos.x + tan.x * hw) * cellsPerMeter_;
                sd.coords.s[3] = pb.mapY + (pos.y + tan.y * hw) * cellsPerMeter_;
                sd.color = dc;
                sd.halfThickness = ht;
                segs.push_back(sd);
            }
            // Swing arc hint
            {
                float swingLen = door.width * 0.7f;
                float sign = door.swingLeft ? 1.0f : -1.0f;
                SegmentData sd;
                sd.coords.s[0] = pb.mapX + (pos.x - tan.x * hw) * cellsPerMeter_;
                sd.coords.s[1] = pb.mapY + (pos.y - tan.y * hw) * cellsPerMeter_;
                sd.coords.s[2] = pb.mapX + (pos.x - tan.x * hw + norm.x * swingLen * sign) * cellsPerMeter_;
                sd.coords.s[3] = pb.mapY + (pos.y - tan.y * hw + norm.y * swingLen * sign) * cellsPerMeter_;
                sd.color = dc;
                sd.halfThickness = 0.4f;
                segs.push_back(sd);
            }
        }

        // ── windows ──
        for (const auto& win : plan.windows) {
            const Wall* wall = nullptr;
            for (const auto& w : plan.walls)
                if (w.id == win.wallId) { wall = &w; break; }
            if (!wall) continue;

            ImVec2 pos = wall->pointAt(win.positionOnWall);
            ImVec2 tan = wall->tangentAt(win.positionOnWall);
            float  hw  = win.width * 0.5f;
            cl_float4 wc = imcolToFloat4(win.color);
            float ht = wall->thickness * cellsPerMeter_ * 0.5f;
            if (ht < 0.5f) ht = 0.5f;

            SegmentData sd;
            sd.coords.s[0] = pb.mapX + (pos.x - tan.x * hw) * cellsPerMeter_;
            sd.coords.s[1] = pb.mapY + (pos.y - tan.y * hw) * cellsPerMeter_;
            sd.coords.s[2] = pb.mapX + (pos.x + tan.x * hw) * cellsPerMeter_;
            sd.coords.s[3] = pb.mapY + (pos.y + tan.y * hw) * cellsPerMeter_;
            sd.color = wc;
            sd.halfThickness = ht;
            segs.push_back(sd);
        }

        // ── staircases ──
        for (const auto& stair : plan.staircases) {
            auto pts = stair.getSampledPoints();
            cl_float4 sc = imcolToFloat4(stair.color);
            float ht = stair.width * cellsPerMeter_ * 0.5f;
            if (ht < 0.5f) ht = 0.5f;

            // Centerline
            for (size_t i = 1; i < pts.size(); ++i) {
                SegmentData sd;
                sd.coords.s[0] = pb.mapX + pts[i-1].x * cellsPerMeter_;
                sd.coords.s[1] = pb.mapY + pts[i-1].y * cellsPerMeter_;
                sd.coords.s[2] = pb.mapX + pts[i  ].x * cellsPerMeter_;
                sd.coords.s[3] = pb.mapY + pts[i  ].y * cellsPerMeter_;
                sd.color = sc;
                sd.halfThickness = ht;
                segs.push_back(sd);
            }
            // Step lines
            if (stair.numSteps > 0 && stair.length() > 0) {
                for (int step = 0; step <= stair.numSteps; ++step) {
                    float t   = (float)step / (float)stair.numSteps;
                    ImVec2 p  = stair.pointAt(t);
                    ImVec2 n  = stair.normalAt(t);
                    float  shw = stair.width * 0.5f;
                    SegmentData sd;
                    sd.coords.s[0] = pb.mapX + (p.x - n.x * shw) * cellsPerMeter_;
                    sd.coords.s[1] = pb.mapY + (p.y - n.y * shw) * cellsPerMeter_;
                    sd.coords.s[2] = pb.mapX + (p.x + n.x * shw) * cellsPerMeter_;
                    sd.coords.s[3] = pb.mapY + (p.y + n.y * shw) * cellsPerMeter_;
                    sd.color = sc;
                    sd.halfThickness = 0.3f;
                    segs.push_back(sd);
                }
            }
        }

        // ── furniture (4 edge segments per item) ──
        for (const auto& furn : plan.furniture) {
            ImVec2 corners[4];
            furn.getCorners(corners);
            cl_float4 fc = imcolToFloat4(furn.color);
            for (int e = 0; e < 4; ++e) {
                int next = (e + 1) % 4;
                SegmentData sd;
                sd.coords.s[0] = pb.mapX + corners[e   ].x * cellsPerMeter_;
                sd.coords.s[1] = pb.mapY + corners[e   ].y * cellsPerMeter_;
                sd.coords.s[2] = pb.mapX + corners[next].x * cellsPerMeter_;
                sd.coords.s[3] = pb.mapY + corners[next].y * cellsPerMeter_;
                sd.color = fc;
                sd.halfThickness = 0.3f;
                segs.push_back(sd);
            }
        }
    }

    // ================================================================
    // 3. Flatten segments into parallel GPU arrays
    // ================================================================

    int totalSegCount = 0;
    std::vector<cl_float4> allCoords;
    std::vector<cl_float4> allColors;
    std::vector<float>     allHalfThick;
    std::vector<cl_float4> allBuildingBounds;
    std::vector<cl_int>    allBuildingSegStart;
    std::vector<cl_int>    allBuildingSegEnd;

    for (size_t bi = 0; bi < placedBuildings.size(); ++bi) {
        allBuildingSegStart.push_back(totalSegCount);
        for (const auto& sd : buildingSegs[bi]) {
            allCoords.push_back(sd.coords);
            allColors.push_back(sd.color);
            allHalfThick.push_back(sd.halfThickness);
            ++totalSegCount;
        }
        allBuildingSegEnd.push_back(totalSegCount);

        const auto& pb = placedBuildings[bi];
        cl_float4 bounds;
        bounds.s[0] = pb.minX;  bounds.s[1] = pb.minY;
        bounds.s[2] = pb.maxX;  bounds.s[3] = pb.maxY;
        allBuildingBounds.push_back(bounds);
    }
    int buildingCount = static_cast<int>(placedBuildings.size());

    PLOGI << "BuildingLayer: rasterizing " << totalSegCount << " segments + "
          << totalRoomCount << " rooms from " << buildingCount << " buildings"
          << " (cellsPerMeter=" << cellsPerMeter_ << ")";
    if (buildingCount > 0) {
        const auto& first = placedBuildings[0];
        PLOGI << "BuildingLayer: first building at (" << first.mapX << ", " << first.mapY
              << ") bounds (" << first.minX << "," << first.minY
              << ")-(" << first.maxX << "," << first.maxY << ")";
    }

    // ================================================================
    // 4. Upload to GPU and run the kernel
    // ================================================================

    static cl_program program = nullptr;
    static cl_kernel  kernel  = nullptr;
    try {
        OpenCLContext::get().createProgram(program, "Kernels/Buildings.cl");
        OpenCLContext::get().createKernelFromProgram(kernel, program, "drawBuildings");
    } catch (const std::runtime_error& e) {
        PLOGE << "BuildingLayer OpenCL init error: " << e.what();
        // Clear to background so we get a valid (visible) buffer
        cl_float4 bg = {{0.88f, 0.85f, 0.78f, 1.0f}};
        clEnqueueFillBuffer(queue, colorBuffer_, &bg,
                            sizeof(cl_float4), 0, outSize, 0, nullptr, nullptr);
        return;
    }

    // Helper: create read-only buffer, or a tiny dummy if the vector is empty
    auto makeReadBuf = [&](const void* data, size_t elemSize, size_t count,
                           const char* tag) -> cl_mem
    {
        if (count == 0) {
            // Kernel still needs a valid cl_mem — allocate 1 element
            return OpenCLContext::get().createBuffer(
                CL_MEM_READ_ONLY, elemSize, nullptr, &err, tag);
        }
        return OpenCLContext::get().createBuffer(
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            elemSize * count, const_cast<void*>(data), &err, tag);
    };

    // Room buffers
    cl_mem roomVertBuf   = makeReadBuf(flatRoomVerts.data(),  sizeof(cl_float2), flatRoomVerts.size(),  "BL roomVerts");
    cl_mem roomStartBuf  = makeReadBuf(roomVertStart.data(),  sizeof(cl_int),    roomVertStart.size(),  "BL roomStart");
    cl_mem roomCountBuf  = makeReadBuf(roomVertCount.data(),  sizeof(cl_int),    roomVertCount.size(),  "BL roomCount");
    cl_mem roomColorBuf  = makeReadBuf(roomFillColors.data(), sizeof(cl_float4), roomFillColors.size(), "BL roomColors");
    cl_mem roomBoundsBuf = makeReadBuf(roomBoundsVec.data(),  sizeof(cl_float4), roomBoundsVec.size(),  "BL roomBounds");

    // Segment buffers
    cl_mem coordsBuf   = makeReadBuf(allCoords.data(),     sizeof(cl_float4), allCoords.size(),     "BL coords");
    cl_mem colorsBuf   = makeReadBuf(allColors.data(),     sizeof(cl_float4), allColors.size(),     "BL colors");
    cl_mem thickBuf    = makeReadBuf(allHalfThick.data(),  sizeof(float),     allHalfThick.size(),  "BL thick");

    // Building culling buffers
    cl_mem bBoundsBuf  = makeReadBuf(allBuildingBounds.data(),   sizeof(cl_float4), allBuildingBounds.size(),   "BL bBounds");
    cl_mem bSegStartBuf= makeReadBuf(allBuildingSegStart.data(), sizeof(cl_int),    allBuildingSegStart.size(), "BL bSegStart");
    cl_mem bSegEndBuf  = makeReadBuf(allBuildingSegEnd.data(),   sizeof(cl_int),    allBuildingSegEnd.size(),   "BL bSegEnd");

    // Set kernel arguments (must match Buildings.cl signature exactly)
    int arg = 0;
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &roomVertBuf);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &roomStartBuf);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &roomCountBuf);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &roomColorBuf);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &roomBoundsBuf);
    clSetKernelArg(kernel, arg++, sizeof(int),    &totalRoomCount);

    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &coordsBuf);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &colorsBuf);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &thickBuf);
    clSetKernelArg(kernel, arg++, sizeof(int),    &totalSegCount);

    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &bBoundsBuf);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &bSegStartBuf);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &bSegEndBuf);
    clSetKernelArg(kernel, arg++, sizeof(int),    &buildingCount);

    clSetKernelArg(kernel, arg++, sizeof(int),    &latRes);
    clSetKernelArg(kernel, arg++, sizeof(int),    &lonRes);
    clSetKernelArg(kernel, arg++, sizeof(cl_mem), &colorBuffer_);

    // Enqueue
    size_t global[2] = { (size_t)latRes, (size_t)lonRes };
    err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global,
                                 nullptr, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOGE << "BuildingLayer: clEnqueueNDRangeKernel failed (" << err << ")";
    }
    clFinish(queue);

    // Release temporary buffers
    OpenCLContext::get().releaseMem(roomVertBuf);
    OpenCLContext::get().releaseMem(roomStartBuf);
    OpenCLContext::get().releaseMem(roomCountBuf);
    OpenCLContext::get().releaseMem(roomColorBuf);
    OpenCLContext::get().releaseMem(roomBoundsBuf);
    OpenCLContext::get().releaseMem(coordsBuf);
    OpenCLContext::get().releaseMem(colorsBuf);
    OpenCLContext::get().releaseMem(thickBuf);
    OpenCLContext::get().releaseMem(bBoundsBuf);
    OpenCLContext::get().releaseMem(bSegStartBuf);
    OpenCLContext::get().releaseMem(bSegEndBuf);
}
