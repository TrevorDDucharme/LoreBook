

struct HeightDeltaLayer {
    std::unordered_map<int, float> modifiedSamples; // sample index → delta
};

struct ChunkLOD {
    int lodLevel;
    int resolution; // pixels per meter
    std::vector<float> baseHeight; // procedural height or LOD0 downsample
    HeightDeltaLayer edits;         // applied delta edits
};

struct WorldChunk {
    
    ChunkCoord coord;
    std::vector<ChunkLOD> lods; // LOD0 = editable, others derived
};