const std = @import("std");
const zm = @import("zmath");

const Self = @This();

position: zm.Vec,
yaw: f32,
pitch: f32,
fov_y: f32,
aspect: f32,
near: f32,
far: f32,
view: zm.Mat,
projection: zm.Mat,
view_projection: zm.Mat,

pub fn init(
    position: zm.Vec,
    yaw: f32,
    pitch: f32,
    fov_y: f32,
    aspect: f32,
    near: f32,
    far: f32,
) Self {
    var cam: Self = undefined;
    cam.position = position;
    cam.yaw = yaw;
    cam.pitch = pitch;
    cam.fov_y = fov_y;
    cam.aspect = aspect;
    cam.near = near;
    cam.far = far;
    cam.updateProjection();
    cam.update();
    return cam;
}

pub fn initLookAt(
    position: zm.Vec,
    target: zm.Vec,
    fov_y: f32,
    aspect: f32,
    near: f32,
    far: f32,
) Self {
    const dir = target - position;
    const len = @sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    const pitch: f32 = if (len > 0) std.math.asin(f32, dir[1] / len) else 0;
    const yaw: f32 = if (len > 0) std.math.atan2(f32, dir[0], dir[2]) else 0;
    return init(position, yaw, pitch, fov_y, aspect, near, far);
}

pub fn setPerspective(self: *Self, fov_y: f32, aspect: f32, near: f32, far: f32) void {
    self.fov_y = fov_y;
    self.aspect = aspect;
    self.near = near;
    self.far = far;
    self.updateProjection();
}

pub fn setAspect(self: *Self, aspect: f32) void {
    self.aspect = aspect;
    self.updateProjection();
}

fn updateProjection(self: *Self) void {
    var proj = zm.perspectiveFovRhGl(self.fov_y, self.aspect, self.near, self.far);
    proj[1] *= zm.f32x4(1, -1, 1, 1);
    self.projection = proj;
}

pub fn update(self: *Self) void {
    const cos_p = @cos(self.pitch);
    const sin_p = @sin(self.pitch);
    const cos_y = @cos(self.yaw);
    const sin_y = @sin(self.yaw);

    const fwd = zm.f32x4(sin_y * cos_p, sin_p, cos_y * cos_p, 0);
    const world_up = zm.f32x4(0, 1, 0, 0);

    // Standard right-handed view matrix (GLM convention):
    //   neg_fwd = eye - target  (direction from target toward eye)
    //   right   = cross(neg_fwd, up)
    //   up      = cross(right, neg_fwd)
    const neg_fwd = -fwd;
    const r = zm.normalize3(zm.cross3(neg_fwd, world_up));
    const u = zm.normalize3(zm.cross3(r, neg_fwd));

    self.view = zm.Mat{
        zm.f32x4(r[0], u[0], neg_fwd[0], 0),
        zm.f32x4(r[1], u[1], neg_fwd[1], 0),
        zm.f32x4(r[2], u[2], neg_fwd[2], 0),
        zm.f32x4(-zm.dot3(r, self.position)[0], -zm.dot3(u, self.position)[0], -zm.dot3(neg_fwd, self.position)[0], 1),
    };
    self.view_projection = zm.mul(self.view, self.projection);
}

pub fn rotate(self: *Self, yaw_delta: f32, pitch_delta: f32) void {
    self.yaw += yaw_delta;
    self.pitch += pitch_delta;
    self.pitch = std.math.clamp(self.pitch, -std.math.pi / 2.0 + 0.01, std.math.pi / 2.0 - 0.01);
}

pub fn translate(self: *Self, offset: zm.Vec) void {
    self.position += offset;
}

pub fn forward(self: Self) zm.Vec {
    return zm.normalize3(zm.f32x4(
        @sin(self.yaw) * @cos(self.pitch),
        @sin(self.pitch),
        @cos(self.yaw) * @cos(self.pitch),
        0,
    ));
}

pub fn right(self: Self) zm.Vec {
    return zm.normalize3(zm.cross3(zm.f32x4(0, 1, 0, 0), self.forward()));
}

pub fn up(self: Self) zm.Vec {
    return zm.cross3(self.forward(), self.right());
}
