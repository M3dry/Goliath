const std = @import("std");
const zprobe = @import("zprobe");
const zglfw = @import("zglfw");

const Self = @This();

pub const Key = zglfw.Key;
pub const MouseButton = zglfw.MouseButton;
pub const Mods = zglfw.Mods;
pub const Action = zglfw.Action;

pub const CaptureFilter = struct {
    keyboard: bool = false,
    mouse: bool = false,
};

const max_key_value = blk: {
    var max: usize = 0;
    for (@typeInfo(Key).@"enum".fields) |field| {
        const val = @as(c_int, @intCast(field.value));
        if (val > 0) {
            max = @max(max, @as(usize, @intCast(val)));
        }
    }
    break :blk max;
};

const key_count = max_key_value + 1;

const max_mouse_value = blk: {
    var max: usize = 0;
    for (@typeInfo(MouseButton).@"enum".fields) |field| {
        const val = @as(c_int, @intCast(field.value));
        if (val > 0) {
            max = @max(max, @as(usize, @intCast(val)));
        }
    }
    break :blk max;
};
const mouse_count = max_mouse_value + 1;

const Event = union(enum) {
    key: struct { key: Key, scancode: c_int, action: Action, mods: Mods },
    mouse_button: struct { button: MouseButton, action: Action, mods: Mods },
    mouse_move: struct { x: f64, y: f64 },
    mouse_scroll: struct { x: f64, y: f64 },
    char_input: struct { codepoint: u21 },
};

events: [256]Event = undefined,
event_count: usize = 0,

key_down: std.bit_set.ArrayBitSet(usize, key_count) = undefined,
key_just_pressed: std.bit_set.ArrayBitSet(usize, key_count) = undefined,
key_just_released: std.bit_set.ArrayBitSet(usize, key_count) = undefined,
key_repeated: std.bit_set.ArrayBitSet(usize, key_count) = undefined,

mouse_down: std.bit_set.IntegerBitSet(mouse_count) = undefined,
mouse_just_pressed: std.bit_set.IntegerBitSet(mouse_count) = undefined,
mouse_just_released: std.bit_set.IntegerBitSet(mouse_count) = undefined,
mouse_just_pressed_mods: [mouse_count]Mods = undefined,

mouse_x: f64 = 0,
mouse_y: f64 = 0,
mouse_delta_x: f64 = 0,
mouse_delta_y: f64 = 0,

scroll_x: f64 = 0,
scroll_y: f64 = 0,

char_events: [64]u21 = undefined,
char_count: usize = 0,

key_just_pressed_mods: [key_count]Mods = undefined,
last_just_pressed: ?Key = null,

pub fn init(self: *Self, window: *zglfw.Window) void {
    self.* = .{};
    self.key_down = .initEmpty();
    self.key_just_pressed = .initEmpty();
    self.key_just_released = .initEmpty();
    self.key_repeated = .initEmpty();
    self.mouse_down = .initEmpty();
    self.mouse_just_pressed = .initEmpty();
    self.mouse_just_released = .initEmpty();
    @memset(&self.mouse_just_pressed_mods, .{});
    @memset(&self.key_just_pressed_mods, .{});
    const pos = window.getCursorPos();
    self.mouse_x = pos[0];
    self.mouse_y = pos[1];
    window.setUserPointer(@ptrCast(self));
    _ = window.setKeyCallback(keyCallback);
    _ = window.setMouseButtonCallback(mouseButtonCallback);
    _ = window.setCursorPosCallback(cursorPosCallback);
    _ = window.setScrollCallback(scrollCallback);
    _ = window.setCharCallback(charCallback);
}

pub fn deinit(self: *Self, window: *zglfw.Window) void {
    _ = window.setKeyCallback(null);
    _ = window.setMouseButtonCallback(null);
    _ = window.setCursorPosCallback(null);
    _ = window.setScrollCallback(null);
    _ = window.setCharCallback(null);
    window.setUserPointer(null);
    self.* = undefined;
}

pub fn update(self: *Self, capture: CaptureFilter) void {
    self.key_just_pressed = .initEmpty();
    self.key_just_released = .initEmpty();
    self.key_repeated = .initEmpty();
    self.mouse_just_pressed = .initEmpty();
    self.mouse_just_released = .initEmpty();
    self.mouse_delta_x = 0;
    self.mouse_delta_y = 0;
    self.scroll_x = 0;
    self.scroll_y = 0;
    self.char_count = 0;
    self.last_just_pressed = null;

    for (self.events[0..self.event_count]) |event| {
        switch (event) {
            .key => |ke| {
                if (ke.key == .unknown) continue;
                if (capture.keyboard) continue;
                const idx: usize = @intCast(@intFromEnum(ke.key));
                switch (ke.action) {
                    .press => {
                        self.key_down.set(idx);
                        self.key_just_pressed.set(idx);
                        self.key_just_pressed_mods[idx] = ke.mods;
                        self.last_just_pressed = ke.key;
                    },
                    .release => {
                        self.key_down.unset(idx);
                        self.key_just_released.set(idx);
                    },
                    .repeat => {
                        self.key_repeated.set(idx);
                    },
                }
            },
            .mouse_button => |mb| {
                if (capture.mouse) continue;
                const idx: usize = @intCast(@intFromEnum(mb.button));
                switch (mb.action) {
                    .press => {
                        self.mouse_down.set(idx);
                        self.mouse_just_pressed.set(idx);
                        self.mouse_just_pressed_mods[idx] = mb.mods;
                    },
                    .release => {
                        self.mouse_down.unset(idx);
                        self.mouse_just_released.set(idx);
                    },
                    .repeat => {},
                }
            },
            .mouse_move => |mm| {
                if (capture.mouse) continue;
                self.mouse_delta_x += mm.x - self.mouse_x;
                self.mouse_delta_y += mm.y - self.mouse_y;
                self.mouse_x = mm.x;
                self.mouse_y = mm.y;
            },
            .mouse_scroll => |ms| {
                if (capture.mouse) continue;
                self.scroll_x += ms.x;
                self.scroll_y += ms.y;
            },
            .char_input => |ci| {
                if (capture.keyboard) continue;
                if (self.char_count < self.char_events.len) {
                    self.char_events[self.char_count] = ci.codepoint;
                    self.char_count += 1;
                }
            },
        }
    }
    self.event_count = 0;
}

pub fn isDown(self: *const Self, key: Key) bool {
    if (key == .unknown) return false;
    return self.key_down.isSet(@intCast(@intFromEnum(key)));
}

pub fn justPressed(self: *const Self, key: Key) bool {
    if (key == .unknown) return false;
    return self.key_just_pressed.isSet(@intCast(@intFromEnum(key)));
}

pub fn justReleased(self: *const Self, key: Key) bool {
    if (key == .unknown) return false;
    return self.key_just_released.isSet(@intCast(@intFromEnum(key)));
}

pub fn isRepeated(self: *const Self, key: Key) bool {
    if (key == .unknown) return false;
    return self.key_repeated.isSet(@intCast(@intFromEnum(key)));
}

pub fn anyJustPressed(self: *const Self) ?Key {
    return self.last_just_pressed;
}

pub fn anyKeyDown(self: *const Self) bool {
    return self.key_down.findFirstSet() != null;
}

pub fn mousePos(self: *const Self) struct { x: f64, y: f64 } {
    return .{ .x = self.mouse_x, .y = self.mouse_y };
}

pub fn mouseDelta(self: *const Self) struct { x: f64, y: f64 } {
    return .{ .x = self.mouse_delta_x, .y = self.mouse_delta_y };
}

pub fn setMousePos(self: *Self, x: f64, y: f64) void {
    self.mouse_x = x;
    self.mouse_y = y;
}

pub fn scrollDelta(self: *const Self) struct { x: f64, y: f64 } {
    return .{ .x = self.scroll_x, .y = self.scroll_y };
}

pub fn isMouseDown(self: *const Self, button: MouseButton) bool {
    return self.mouse_down.isSet(@intCast(@intFromEnum(button)));
}

pub fn mouseJustPressed(self: *const Self, button: MouseButton) bool {
    return self.mouse_just_pressed.isSet(@intCast(@intFromEnum(button)));
}

pub fn mouseJustReleased(self: *const Self, button: MouseButton) bool {
    return self.mouse_just_released.isSet(@intCast(@intFromEnum(button)));
}

pub fn justPressedWith(self: *const Self, key: Key) ?Mods {
    if (key == .unknown) return null;
    const idx: usize = @intCast(@intFromEnum(key));
    if (!self.key_just_pressed.isSet(idx)) return null;
    return self.key_just_pressed_mods[idx];
}

pub fn mouseJustPressedWith(self: *const Self, button: MouseButton) ?Mods {
    const idx: usize = @intCast(@intFromEnum(button));
    if (!self.mouse_just_pressed.isSet(idx)) return null;
    return self.mouse_just_pressed_mods[idx];
}

pub fn charEvents(self: *const Self) []const u21 {
    return self.char_events[0..self.char_count];
}

pub fn clearCharEvents(self: *Self) void {
    self.char_count = 0;
}

fn keyCallback(window: *zglfw.Window, key: Key, scancode: c_int, action: Action, mods: Mods) callconv(.c) void {
    if (key == .unknown) return;
    const state = window.getUserPointer(Self) orelse return;
    if (state.event_count >= state.events.len) {
        zprobe.event(.warn, "key callback: event buffer full", .{});
        return;
    }
    state.events[state.event_count] = .{ .key = .{ .key = key, .scancode = scancode, .action = action, .mods = mods } };
    state.event_count += 1;
}

fn mouseButtonCallback(window: *zglfw.Window, button: MouseButton, action: Action, mods: Mods) callconv(.c) void {
    const state = window.getUserPointer(Self) orelse return;
    if (state.event_count >= state.events.len) {
        zprobe.event(.warn, "mouse button callback: event buffer full", .{});
        return;
    }
    state.events[state.event_count] = .{ .mouse_button = .{ .button = button, .action = action, .mods = mods } };
    state.event_count += 1;
}

fn cursorPosCallback(window: *zglfw.Window, xpos: f64, ypos: f64) callconv(.c) void {
    const state = window.getUserPointer(Self) orelse return;
    if (state.event_count >= state.events.len) {
        zprobe.event(.warn, "cursor pos callback: event buffer full", .{});
        return;
    }
    state.events[state.event_count] = .{ .mouse_move = .{ .x = xpos, .y = ypos } };
    state.event_count += 1;
}

fn scrollCallback(window: *zglfw.Window, xoffset: f64, yoffset: f64) callconv(.c) void {
    const state = window.getUserPointer(Self) orelse return;
    if (state.event_count >= state.events.len) {
        zprobe.event(.warn, "scroll callback: event buffer full", .{});
        return;
    }
    state.events[state.event_count] = .{ .mouse_scroll = .{ .x = xoffset, .y = yoffset } };
    state.event_count += 1;
}

fn charCallback(window: *zglfw.Window, codepoint: u32) callconv(.c) void {
    const state = window.getUserPointer(Self) orelse return;
    if (codepoint > 0x10FFFF) return;
    if (state.event_count >= state.events.len) {
        zprobe.event(.warn, "char callback: event buffer full", .{});
        return;
    }
    state.events[state.event_count] = .{ .char_input = .{ .codepoint = @intCast(codepoint) } };
    state.event_count += 1;
}
