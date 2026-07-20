const std = @import("std");

pub const Offsets = struct {
    start: u32,
    stride: u32,
    indices_offset: u32,
    position_offset: u32,
    normal_offset: u32,
    tangent_offset: u32,
    texcoord0_offset: u32,
    texcoord1_offset: u32,
    texcoord2_offset: u32,
    texcoord3_offset: u32,
    // in the same buffer the vertex data starting at `start`

    pub const indexed_tangents_bit: u32 = 0x80000000;
    pub const stride_mask: u32 = 0x7FFFFFFF;

    pub fn indexedTangents(self: Offsets) bool {
        return (self.stride & indexed_tangents_bit) != 0;
    }
};

pub const MeshDesc = struct {
    lod_offset: u32,
    lod_count: u32,
    current_lod: LODEntry, // if lod_count == 1, always correct
    aabb_min_x: f32, aabb_min_y: f32, aabb_min_z: f32,
    aabb_max_x: f32, aabb_max_y: f32, aabb_max_z: f32,
};

pub const LODEntry = struct {
    buffer_address: u64, // gpu reference to Offsets
    vertex_count: u32,
    index_count: u32,
    material_schema: u32,
    material_instance: u32,
    error_metric: f32,
};

pub const InstanceData = struct {
    transform: [4]@Vector(4, f32),
    mesh_desc_ix: u32,
    material_schema: u32,
    material_instance: u32,
};

pub const CulledDrawCmd = struct {
    vertex_count: u32,
    instance_count: u32,
    first_vertex: u32,
    first_instance: u32,
    instance_data_ix: u32,
};

// Buffers:
// MeshDesc[]
// LODEntry[]
// InstanceData[]
// CulledDrawCmd[]
// some number of buffers containing `Offsets` structs at some indices
