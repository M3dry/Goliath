#ifndef _CULLED_DATA_
#define _CULLED_DATA_

#extension GL_EXT_buffer_reference : require

struct CulledDrawCmd {
    uint data[5];
};

layout(buffer_reference, std430) readonly buffer CulledDrawCmds {
    CulledDrawCmd cmd[];
};

struct DrawID {
    VertexData group;
    uint start_offset;
    uint material_id;
    mat4 model_transform;
};

layout(buffer_reference, std430) buffer DrawIDs {
    uint current_size;
    DrawID id[];
};

#endif
