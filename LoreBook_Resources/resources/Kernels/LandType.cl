
#include "Perlin.cl"

typedef struct LandTypeProperties {
  float4 color;
  float water_absorption;
  float water_retention;
  float nutrient_content;
  float thermal_reflectivity;
  float heating_capacity;
  float permeability;
  float erodibility;
  float salinity;
};

__kernel void
landtypeColor(int latitudeResolution, int longitudeResolution,
              __global const struct LandTypeProperties *landtypeProperties,
              int landtypeCount, __global const float *frequency,
              __global const float *lacunarity, __global const int *octaves,
              __global const float *persistence, __global const uint *seed,
              int mode, __global float4 *outRGBA) {
  int latitude = get_global_id(0);
  int longitude = get_global_id(1);
  if (latitude >= latitudeResolution || longitude >= longitudeResolution)
    return;

  int idx = latitude * longitudeResolution + longitude;
  int voxels = latitudeResolution * longitudeResolution;

  if (mode == 0) {
    // Argmax mapping (sample channels on the sphere by lat/long)
    float bestVal = -INFINITY;
    int bestIdx = 0;
    for (int b = 0; b < landtypeCount; ++b) {
      float val = perlin_3d_sphere_sample_channels_util(
          latitude, longitude, latitudeResolution, longitudeResolution, b,
          landtypeCount, frequency, lacunarity, octaves, persistence, seed);
      if (val > bestVal) {
        bestVal = val;
        bestIdx = b;
      }
    }
    float4 col = landtypeProperties[bestIdx].color;
    col.w = 1.0f; // ensure opaque
    outRGBA[idx] = col;
  } else {
    // Exponential falloff blend with argmax fallback to avoid unclassified
    // zones Tune these for stronger/weaker falloff:
    const float sharpness = 20.0f; // higher => sharper peaks
    const float minNormalizedForBlend =
        0.05f; // if best landtype < this, use argmax

    // Find max (best) value and best index (sphere sampling)
    float maxW = -INFINITY;
    int bestIdx = 0;
    for (int b = 0; b < landtypeCount; ++b) {
      float v = perlin_3d_sphere_sample_channels_util(
          latitude, longitude, latitudeResolution, longitudeResolution, b,
          landtypeCount, frequency, lacunarity, octaves, persistence, seed);
      if (v > maxW) {
        maxW = v;
        bestIdx = b;
      }
    }

    // If all zero or negative, fall back to argmax palette color
    if (maxW <= 0.0f) {
      float4 col = landtypeProperties[bestIdx].color;
      col.w = 1.0f;
      outRGBA[idx] = col;
    } else {
      // Compute exponential weights relative to maxW
      float accX = 0.0f, accY = 0.0f, accZ = 0.0f, accW = 0.0f;
      float sumExp = 0.0f;
      for (int b = 0; b < landtypeCount; ++b) {
        float w = perlin_3d_sphere_sample_channels_util(
            latitude, longitude, latitudeResolution, longitudeResolution, b,
            landtypeCount, frequency, lacunarity, octaves, persistence, seed);
        // treat negatives as zero influence
        if (w <= 0.0f)
          continue;
        float wexp = exp(sharpness * (w - maxW)); // max -> exp(0) = 1.0
        float4 c = landtypeProperties[b].color;
        accX += c.x * wexp;
        accY += c.y * wexp;
        accZ += c.z * wexp;
        accW += c.w * wexp;
        sumExp += wexp;
      }

      if (sumExp <= 0.0f) {
        // Defensive fallback (shouldn't happen)
        float4 col = landtypeProperties[bestIdx].color;
        col.w = 1.0f;
        outRGBA[idx] = col;
      } else {
        // If the best landtype's normalized weight is too small, use argmax to
        // ensure classification best normalized weight = 1.0 / sumExp (since
        // best wexp is 1.0)
        if ((1.0f / sumExp) < minNormalizedForBlend) {
          float4 col = landtypeProperties[bestIdx].color;
          col.w = 1.0f;
          outRGBA[idx] = col;
        } else {
          float4 col = (float4)(accX / sumExp, accY / sumExp, accZ / sumExp,
                                accW / sumExp);
          col.w = 1.0f;
          outRGBA[idx] = col;
        }
      }
    }
  }
}

__kernel void
landtype(int latitudeResolution, int longitudeResolution, int channels,
         __global const struct LandTypeProperties *landtypeProperties,
         int landtypeCount, __global const float *frequency,
         __global const float *lacunarity, __global const int *octaves,
         __global const float *persistence, __global const uint *seed, int mode,
         __global float *out) {
  int latitude = get_global_id(0);
  int longitude = get_global_id(1);
  int channel = get_global_id(2);
  if (latitude >= latitudeResolution || longitude >= longitudeResolution ||
      channel >= channels)
    return;

  int idx = latitude * longitudeResolution * channels + longitude * channels +
            channel;
  int voxels = latitudeResolution * longitudeResolution * channels;

  if (mode == 0) {
    // Argmax mapping (sample channels on the sphere by lat/long)
    float bestVal = -INFINITY;
    int bestIdx = 0;
    for (int b = 0; b < landtypeCount; ++b) {
      float val = perlin_3d_sphere_sample_channels_util(
          latitude, longitude, latitudeResolution, longitudeResolution, b,
          landtypeCount, frequency, lacunarity, octaves, persistence, seed);
      if (val > bestVal) {
        bestVal = val;
        bestIdx = b;
      }
    }
    // DO NOT USE LANDTYPEPROPERTIES this is not the color.
    // this is a float output with landtypecount channels
    // that means we need to output [0,0,0,0,....1,0,0] for landtype index
    // bestIdx with 1 at bestIdx position
    out[idx] = (float)(bestIdx == channel ? 1.0f : 0.0f);
  } else {
    // Exponential falloff blend with argmax fallback to avoid unclassified
    // zones Tune these for stronger/weaker falloff:
    const float sharpness = 20.0f; // higher => sharper peaks
    const float minNormalizedForBlend =
        0.05f; // if best landtype < this, use argmax

    // Find max (best) value and best index (sphere sampling)
    float maxW = -INFINITY;
    int bestIdx = 0;
    for (int b = 0; b < landtypeCount; ++b) {
      float v = perlin_3d_sphere_sample_channels_util(
          latitude, longitude, latitudeResolution, longitudeResolution, b,
          landtypeCount, frequency, lacunarity, octaves, persistence, seed);
      if (v > maxW) {
        maxW = v;
        bestIdx = b;
      }
    }

    // If all zero or negative, fall back to argmax
    if (maxW <= 0.0f) {
      out[idx] = (float)(bestIdx == channel ? 1.0f : 0.0f);
    } else {
      // Compute exponential weights for all landtypes and sum them
      // We store weights in a local array (max 64 landtypes supported)
      float weights[64];
      float sumExp = 0.0f;

      for (int b = 0; b < landtypeCount; ++b) {
        float w = perlin_3d_sphere_sample_channels_util(
            latitude, longitude, latitudeResolution, longitudeResolution, b,
            landtypeCount, frequency, lacunarity, octaves, persistence, seed);

        // treat negatives as zero influence
        if (w <= 0.0f) {
          weights[b] = 0.0f;
        } else {
          weights[b] = exp(sharpness * (w - maxW)); // max -> exp(0) = 1.0
          sumExp += weights[b];
        }
      }

      if (sumExp <= 0.0f) {
        // Defensive fallback (shouldn't happen)
        out[idx] = (float)(bestIdx == channel ? 1.0f : 0.0f);
      } else {
        // If the best landtype's normalized weight is too small, use argmax to
        // ensure classification. Best normalized weight = weights[bestIdx] /
        // sumExp (since best wexp is 1.0 when w == maxW)
        if ((weights[bestIdx] / sumExp) < minNormalizedForBlend) {
          out[idx] = (float)(bestIdx == channel ? 1.0f : 0.0f);
        } else {
          // Output normalized softmax weight for this channel
          out[idx] = weights[channel] / sumExp;
        }
      }
    }
  }
}