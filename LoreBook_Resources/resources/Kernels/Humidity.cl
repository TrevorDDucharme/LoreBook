#include "Latitude.cl"
#include "LandType.cl"
#include "Perlin.cl"

__kernel void humidity_map(
    const int latitudeResolution,
    const int longitudeResolution,
    __global const struct LandTypeProperties* landtypeProperties,
    const int landtypeCount,
    __global const float* frequency,
    __global const float* lacunarity,
    __global const int* octaves,
    __global const float* persistence,
    __global const uint* seed,
    __global const float* elevation,
    __global const float* watertable,
    __global const float* rivers,
    __global const float* temperature,
    __global float* output)
{
    int latitude = get_global_id(0);
    int longitude = get_global_id(1);
    
    if (latitude >= latitudeResolution || longitude >= longitudeResolution)
        return;
    
    int index = latitude + longitude * latitudeResolution;
    
    // --- 1. Read all input data ---
    float elev = elevation[index];
    float water_depth = watertable[index];
    float river_flow = rivers[index];  
    float temp = temperature[index];
    
    // --- 2. Sample landtype properties by recomputing landtype distribution ---
    // Find dominant landtype and blend properties
    float maxW = -INFINITY;
    int bestIdx = 0;
    float weights[64]; // Max 64 landtypes
    float sumExp = 0.0f;
    const float sharpness = 20.0f;
    
    for (int b = 0; b < landtypeCount; ++b) {
        float w = perlin_3d_sphere_sample_channels_util(
            latitude, longitude, latitudeResolution, longitudeResolution, b,
            landtypeCount, frequency, lacunarity, octaves, persistence, seed);
        if (w > maxW) {
            maxW = w;
            bestIdx = b;
        }
        if (w <= 0.0f) {
            weights[b] = 0.0f;
        } else {
            weights[b] = exp(sharpness * (w - maxW));
            sumExp += weights[b];
        }
    }
    
    // Blend landtype properties based on weights
    float water_retention = 0.0f;
    float permeability = 0.0f;
    
    if (sumExp > 0.0f) {
        for (int b = 0; b < landtypeCount; ++b) {
            float normalized_weight = weights[b] / sumExp;
            water_retention += landtypeProperties[b].water_retention * normalized_weight;
            permeability += landtypeProperties[b].permeability * normalized_weight;
        }
    } else {
        // Fallback to best landtype
        water_retention = landtypeProperties[bestIdx].water_retention;
        permeability = landtypeProperties[bestIdx].permeability;
    }
    
    // --- 3. Determine if we're over water or land ---
    bool is_ocean = (water_depth > 0.0f);
    
    if (is_ocean) {
        // OCEAN: Humidity depends on water temperature (evaporation rate)
        // Cold oceans (poles) barely evaporate, warm oceans (tropics) create lots of moisture
        float temp_normalized = (temp + 1.0f) * 0.5f; // 0 to 1
        
        // Evaporation is exponential with temperature - very steep curve
        // Cold water (poles): temp_normalized ~0 → evaporation ~0
        // Warm water (tropics): temp_normalized ~1 → evaporation ~1
        float evaporation_rate = pow(temp_normalized, 3.5f);
        
        // Ocean humidity: cold oceans ~0, warm oceans ~0.9-1.0
        float ocean_humidity = evaporation_rate * 0.95f;
        
        output[index] = clamp(ocean_humidity, 0.0f, 1.0f);
        return;
    }
    
    // --- 4. LAND: Complex interaction of factors ---
    
    // Base aridity from landtype properties
    // Low water_retention + high permeability = arid (water drains away)
    // High water_retention + low permeability = humid (water stays)
    float landtype_aridity = (1.0f - water_retention) * permeability;
    
    // Temperature effect: warm = more evaporation capacity (not necessarily humid)
    float temp_normalized = (temp + 1.0f) * 0.5f;
    
    // Latitude for climate zones: TWO subtropical desert belts at 30°N and 30°S
    float lat_value = latitude_util(latitude, longitude, latitudeResolution, longitudeResolution);
    // Distance from equator: 0 at equator (0.5), 0.2 at subtropics (0.3, 0.7), 0.5 at poles (0.0, 1.0)
    float distance_from_equator = fabs(lat_value - 0.5f);
    
    // Desert belt peaks at subtropics (distance ~0.2), zero at equator and poles
    // Use inverted parabola: peak when distance is 0.2, zero when distance is 0 or 0.5
    float desert_belt = 1.0f - fabs(distance_from_equator - 0.2f) * 5.0f;
    desert_belt = clamp(desert_belt, 0.0f, 1.0f);
    
    // Equatorial boost: Equator (lat 0.5) should be HUMID (rainforests)
    float equatorial_humidity = 1.0f - distance_from_equator * 2.0f;
    equatorial_humidity = clamp(equatorial_humidity, 0.0f, 1.0f);
    
    // Base aridity: landtype + desert belt - equatorial bonus
    float base_aridity = landtype_aridity * 0.4f + desert_belt * 0.6f - equatorial_humidity * 0.3f;
    base_aridity = clamp(base_aridity, 0.0f, 1.0f);
    
    // Moisture capacity: temperature allows moisture, but cold = frozen/dry
    float moisture_capacity = temp_normalized * fmax(0.0f, 1.0f - elev * 0.7f);
    moisture_capacity = clamp(moisture_capacity, 0.0f, 1.0f);
    
    // --- 5. Atmospheric moisture (weather patterns) ---
    // Use noise to simulate weather patterns and prevailing winds bringing moisture
    float noise = perlin_3d_sphere_sample_util(
        latitude, longitude,
        latitudeResolution, longitudeResolution,
        0.008f,  // lower frequency for large weather systems
        2.0f,
        6,
        0.5f,
        98765u
    );
    float weather_moisture = (noise + 1.0f) * 0.5f; // 0 to 1
    
    // --- 6. Combine factors ---
    // Start with moisture capacity, reduced by aridity
    float base_humidity = (1.0f - base_aridity) * moisture_capacity;
    
    // Add water sources (rivers) - but only if base allows some moisture
    if (river_flow > 0.05f) {
        float river_humidity = clamp(river_flow * 2.0f, 0.0f, 0.9f);
        base_humidity = fmax(base_humidity, river_humidity * (1.0f - base_aridity * 0.5f));
    }
    
    // Weather patterns modulate humidity - allow full range 0.5-1.5x
    base_humidity = base_humidity * (0.5f + weather_moisture * 1.0f);
    
    // Snap very low values to 0 (threshold for true deserts)
    if (base_humidity < 0.05f) {
        base_humidity = 0.0f;
    }
    
    // Final output
    output[index] = clamp(base_humidity, 0.0f, 1.0f);
}
