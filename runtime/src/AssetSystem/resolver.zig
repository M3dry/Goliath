const std = @import("std");

const types = @import("Types.zig");
const Gid = types.Gid;
const Kind = types.Kind;

pub const ResolvedEntry = struct {
    gid: Gid,
    kind: Kind,
    dense: u32,

};

pub const Resolved = []const ResolvedEntry;

pub fn lookup(self: Resolved, gid: Gid) ?struct {Kind, u32} {
    for (self) |entry| {
        if (entry.gid == gid) return .{entry.kind, entry.dense};
    }

    return null;
}

test {
    std.testing.refAllDecls(@This());
}
