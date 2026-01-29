__kernel void concatVolumesSVM(
    __global float* output,
    __global cl_mem* volumes,
    int num_channels,
    int num_voxels
) {
    int i = get_global_id(0);
    if (i >= num_voxels) return;

    for (int c = 0; c < num_channels; c++) {
        __global float* vol = (__global float*)volumes[c];
        output[c * num_voxels + i] = vol[i];
    }
}
