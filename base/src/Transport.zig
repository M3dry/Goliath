//! Public type-erased transport API and backend selection.

const std = @import("std");
const root = @import("root.zig");
const Dedicated = @import("Transport/Dedicated.zig");
const NonDedicated = @import("Transport/NonDedicated.zig");
pub const Interface = @import("Transport/Interface.zig");

pub const Ticket = Interface.Ticket;
pub const GroupTicket = Interface.GroupTicket;
pub const GroupUpload = Interface.GroupUpload;
pub const FreeFn = Interface.FreeFn;
pub const VTable = Interface.VTable;
pub const QueueError = Interface.QueueError;
pub const UploadError = Interface.UploadError;
pub const ImageUploadError = Interface.ImageUploadError;
pub const GroupUploadError = Interface.GroupUploadError;
pub const GroupImageUploadError = Interface.GroupImageUploadError;
pub const GroupCreateError = Interface.GroupCreateError;
pub const InitError = Interface.InitError;
pub const ReadinessError = Interface.ReadinessError;
pub const DrainError = Interface.DrainError;
pub const TimelineError = Interface.TimelineError;

pub const Backend = union(enum) {
    dedicated: Dedicated,
    non_dedicated: NonDedicated,

    pub fn interface(self: *Backend) *Interface {
        return switch (self.*) {
            .dedicated => |*backend| &backend.interface,
            .non_dedicated => |*backend| &backend.interface,
        };
    }
};

pub const BackendTag = @typeInfo(Backend).@"union".tag_type.?;

const Transport = @This();

backend: *Backend,
alloc: std.mem.Allocator,

pub fn init(alloc: std.mem.Allocator, staging_alloc: std.mem.Allocator, backend: BackendTag, ctx: *root.Ctx) InitError!Transport {
    const backend_ptr = try alloc.create(Backend);
    errdefer alloc.destroy(backend_ptr);
    var res: Transport = .{
        .backend = backend_ptr,
        .alloc = alloc,
    };

    res.backend.* = switch (backend) {
        .dedicated => .{ .dedicated = .{} },
        .non_dedicated => .{ .non_dedicated = .{} },
    };
    try res.backend.interface().init(ctx, alloc, staging_alloc);

    return res;
}

pub inline fn deinit(self: *Transport) void {
    self.backend.interface().deinit();
    self.alloc.destroy(self.backend);
}

pub inline fn interface(self: *Transport) *Interface {
    return self.backend.interface();
}

test {
    std.testing.refAllDecls(@This());
}
