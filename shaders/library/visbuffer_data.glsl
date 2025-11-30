#ifndef _VISBUFFER_DATA_
#define _VISBUFFER_DATA_

struct VisFragment {
    uint draw_id;
    uint primitive_id;
};

VisFragment read_vis_fragment(uint fragment) {
    return VisFragment(fragment & 0x3FFF, fragment >> 14);
}

uint write_vis_fragment(VisFragment vis_fragment) {
    uint ret;

    vis_fragment.draw_id &= 0x3FFF;
    vis_fragment.primitive_id &= 0x3FFFF;
    ret = (vis_fragment.primitive_id << 14) | vis_fragment.draw_id;

    return ret;
}

#endif
