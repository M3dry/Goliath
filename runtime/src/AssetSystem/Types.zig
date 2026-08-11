pub const Gid = packed struct(u64) {
    gen: u32,
    slot: u32,
};

// mesh get loaded at init into GPU buffers, omitting the geometry and material
pub const Kind = enum(u8) {
    texture,
    sampled_texture, // sampler description + texture
    material_schema, // reflection data on how to read a material_instance
    material_instance, // n x sampled_texture + material schema
    geometry, // pure geometry buffer with offsets
    mesh, // n x (geometry + material instance) - for each LOD (LODs aren't shared, thus no asset handle for them)
    model, // n x (mesh + transform + ?(skeleton + skin(s))) - skin not stored on mesh because skins are tied to a specific skeleton

    skeleton, // contains animations - no need to have animations be a separate asset since they're specific to a skeleton
};

pub const ReleaseReturn = enum {
    kept,
    released,
};
