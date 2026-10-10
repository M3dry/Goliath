const std = @import("std");

pub fn ConstView(comptime T: type) type {
    const info = @typeInfo(T);
    switch (info) {
        .@"struct" => |s| {
            if (s.backing_integer != null) return T;
            if (s.is_tuple) @compileError("TODO");

            var field_names: [s.fields.len][]const u8 = undefined;
            var field_types: [s.fields.len]type = undefined;
            var field_attrs: [s.fields.len]std.builtin.Type.StructField.Attributes = undefined;

            var same = true;
            for (s.fields, 0..) |field, i| {
                field_names[i] = field.name;

                const CT = ConstView(field.type);
                same &= field.type == CT;
                field_types[i] = CT;
                field_attrs[i] = .{
                    .@"align" = field.alignment,
                    .@"comptime" = field.is_comptime,
                    .default_value_ptr = field.default_value_ptr,
                };
            }

            if (same) return T;
            return @Struct(s.layout, null, &field_names, &field_types, &field_attrs);
        },
        .@"union" => |u| {
            var field_names: [u.fields.len][]const u8 = undefined;
            var field_types: [u.fields.len]type = undefined;
            var field_attrs: [u.fields.len]std.builtin.Type.UnionField.Attributes = undefined;

            var same = true;
            for (u.fields, 0..) |field, i| {
                field_names[i] = field.name;

                const CT = ConstView(field.type);
                same &= CT == field.type;

                field_types[i] = CT;
                field_attrs[i] = .{
                    .@"align" = field.alignment,
                };
            }

            if (same) return T;
            return @Union(u.layout, u.tag_type, &field_names, &field_types, &field_attrs);
        },
        .optional => |o| return ?ConstView(o.child),
        .pointer => |p| {
            const sentinel = if (p.sentinel()) |s| toConstView(s) else null;
            return @Pointer(p.size, .{ .@"addrspace" = p.address_space, .@"align" = p.alignment, .@"allowzero" = p.is_allowzero, .@"const" = true, .@"volatile" = p.is_volatile }, ConstView(p.child), sentinel);
        },
        .vector => |v| return @Vector(v.len, ConstView(v.child)),
        .error_union => @compileError("Can't handle error unions"),
        else => return T,
    }
}

pub inline fn toConstView(x: anytype) ConstView(@TypeOf(x)) {
    const T = @TypeOf(x);
    const CT = ConstView(T);

    return @as(*const CT, @ptrCast(&x)).*;
}
