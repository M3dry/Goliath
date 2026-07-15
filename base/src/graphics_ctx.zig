const vk = @import("vulkan");
const zglfw = @import("zglfw");

const std = @import("std");
const vma = @import("vma.zig").vma;

const Allocator = std.mem.Allocator;

const BaseWrapper = vk.BaseWrapper;
const InstanceWrapper = vk.InstanceWrapper;
const DeviceWrapper = vk.DeviceWrapper;

const Instance = vk.InstanceProxy;
const Device = vk.DeviceProxy;

fn getGlfwInstanceProcAddr(instance: vk.Instance, procname: [*:0]const u8) vk.PfnVoidFunction {
    return zglfw.getInstanceProcAddress(instance, procname);
}

fn checkLayerSupport(alloc: Allocator, vkb: *const BaseWrapper, required_layers: []const [*:0]const u8) !bool {
    const available_layers = try vkb.enumerateInstanceLayerPropertiesAlloc(alloc);
    defer alloc.free(available_layers);

    for (required_layers) |required_layer| {
        for (available_layers) |layer| {
            if (std.mem.eql(u8, std.mem.span(required_layer), std.mem.sliceTo(&layer.layer_name, 0))) {
                break;
            }
        } else return false;
    }

    return true;
}

fn debugUtilsMessengerCallback(severity: vk.DebugUtilsMessageSeverityFlagsEXT, msg_type: vk.DebugUtilsMessageTypeFlagsEXT, callback_data: ?*const vk.DebugUtilsMessengerCallbackDataEXT, _: ?*anyopaque) callconv(.c) vk.Bool32 {
    const type_str = if (msg_type.general_bit_ext) "general" else if (msg_type.validation_bit_ext) "validation" else if (msg_type.performance_bit_ext) "performance" else if (msg_type.device_address_binding_bit_ext) "device addr" else "unknown";

    const message: [*c]const u8 = if (callback_data) |cb_data| cb_data.p_message else "NO MESSAGE!";

    if (severity.verbose_bit_ext or severity.info_bit_ext) {
        std.log.info("[{s}]: {s}", .{type_str, message});
    } else if (severity.warning_bit_ext) {
        std.log.warn("[{s}]: {s}", .{type_str, message});
    } else if (severity.error_bit_ext) {
        std.log.err("[{s}]: {s}", .{type_str, message});
    } else {
        std.log.info("[{s}]: {s}", .{type_str, message});
    }

    return .false;
}

fn checkSuitable(alloc: Allocator, instance: Instance, pdev: vk.PhysicalDevice, surface: vk.SurfaceKHR, required_device_extensions: []const [*:0]const u8) !?struct {vk.PhysicalDevice, vk.PhysicalDeviceProperties, u32, u32, bool} {
    const propsv = try instance.enumerateDeviceExtensionPropertiesAlloc(pdev, null, alloc);
    defer alloc.free(propsv);

    for (required_device_extensions) |ext| {
        for (propsv) |props| {
            if (std.mem.eql(u8, std.mem.span(ext), std.mem.sliceTo(&props.extension_name, 0))) {
                break;
            }
        } else return null;
    }

    var format_count: u32 = undefined;
    _ = try instance.getPhysicalDeviceSurfaceFormatsKHR(pdev, surface, &format_count, null);
    if (format_count == 0) return null;

    var present_mode_count: u32 = undefined;
    _ = try instance.getPhysicalDeviceSurfacePresentModesKHR(pdev, surface, &present_mode_count, null);
    if (present_mode_count == 0) return null;

    const families = try instance.getPhysicalDeviceQueueFamilyPropertiesAlloc(pdev, alloc);
    defer alloc.free(families);

    const graphics_family: u32 = for (families, 0..) |props, i| {
        const family: u32 = @intCast(i);
        if (props.queue_flags.graphics_bit and (try instance.getPhysicalDeviceSurfaceSupportKHR(pdev, family, surface)) == .true) {
            break family;
        }
    } else return null;

    const transport_family: u32 = for (families, 0..) |props, i| {
        const family: u32 = @intCast(i);
        if (props.queue_flags.transfer_bit) {
            if (family != graphics_family) {
                break family;
            }
        }
    } else graphics_family;

    const has_dedicated_transport = transport_family != graphics_family;

    return .{
        pdev,
        instance.getPhysicalDeviceProperties(pdev),
        graphics_family,
        transport_family,
        has_dedicated_transport,
    };
}

pub const GraphicsCtx = struct {
    pub const QueueType = enum {
        graphics,
        transport,
    };

    vkb: BaseWrapper,
    instance: Instance,
    pdev: vk.PhysicalDevice,
    props: vk.PhysicalDeviceProperties,
    dev: Device,
    vma_alloc: vma.VmaAllocator,

    graphics_family: u32,
    graphics_queue: vk.Queue,

    transport_family: u32,
    transport_queue: vk.Queue,
    has_dedicated_transport: bool,

    surface: vk.SurfaceKHR,

    debug_messenger: vk.DebugUtilsMessengerEXT,

    pub fn init(alloc: Allocator, window: *zglfw.Window) !GraphicsCtx {
        const vkb = BaseWrapper.load(getGlfwInstanceProcAddr);

        const required_layers = [_][*:0]const u8{ "VK_LAYER_KHRONOS_validation" };
        if (!try checkLayerSupport(alloc, &vkb, &required_layers))  {
            return error.MissingLayer;
        }
        var extensions: std.ArrayList([*:0]const u8) = .empty;
        defer extensions.deinit(alloc);

        try extensions.append(alloc, vk.extensions.ext_debug_utils.name);
        // try extensions.append(alloc, vk.extensions.khr_portability_enumeration.name);
        // try extensions.append(alloc, vk.extensions.khr_get_physical_device_properties_2.name);

        const glfw_exts = try zglfw.getRequiredInstanceExtensions();
        try extensions.appendSlice(alloc, glfw_exts);

        const instance = try vkb.createInstance(&.{
            .p_application_info = &.{
                .p_application_name = "TODO",
                .application_version = vk.makeApiVersion(0, 0, 0, 0).toU32(),
                .p_engine_name = "Goliath",
                .engine_version = vk.makeApiVersion(0, 0, 1, 0).toU32(),
                .api_version = vk.API_VERSION_1_3.toU32(),
            },
            .enabled_layer_count = required_layers.len,
            .pp_enabled_layer_names = &required_layers,
            .enabled_extension_count = @intCast(extensions.items.len),
            .pp_enabled_extension_names = extensions.items.ptr,
            .flags = .{ .enumerate_portability_bit_khr = true, },
        }, null);

        const vki = try alloc.create(InstanceWrapper);
        errdefer alloc.destroy(vki);
        vki.* = InstanceWrapper.load(instance, vkb.dispatch.vkGetInstanceProcAddr.?);
        const inst = vk.InstanceProxy.init(instance, vki);
        errdefer inst.destroyInstance(null);

        const debug_messenger = try inst.createDebugUtilsMessengerEXT(&.{
            .message_severity = .{
                .info_bit_ext = true,
                .warning_bit_ext = true,
                .error_bit_ext = true,
            },
            .message_type = .{
                .general_bit_ext = true,
                .validation_bit_ext = true,
                .performance_bit_ext = true,
            },
            .pfn_user_callback = &debugUtilsMessengerCallback,
            .p_user_data = null,
        }, null);

        var surface: vk.SurfaceKHR = undefined;
        try zglfw.createWindowSurface(inst.handle, window, null, &surface);
        errdefer inst.destroySurfaceKHR(surface, null);

        const required_device_extensions = [_][*:0]const u8{
            vk.extensions.khr_swapchain.name,
        };

        const pdevs = try inst.enumeratePhysicalDevicesAlloc(alloc);
        defer alloc.free(pdevs);
        const pdev, const props, const graphics_family, const transport_family, const has_dedicated_transport = for (pdevs) |pdev| {
            if (try checkSuitable(alloc, inst, pdev, surface, &required_device_extensions)) |candidate| {
                break candidate;
            }
        } else {
            return error.NoSuitableDevice;
        };

        const priority = [_]f32{1};
        const qci = [_]vk.DeviceQueueCreateInfo{
            .{
                .queue_family_index = graphics_family,
                .queue_count = 1,
                .p_queue_priorities = &priority,
            },
            .{
                .queue_family_index = transport_family,
                .queue_count = 1,
                .p_queue_priorities = &priority,
            }
        };

        var features13: vk.PhysicalDeviceVulkan13Features = .{
            .dynamic_rendering = .true,
            .synchronization_2 = .true,
        };
        var features12: vk.PhysicalDeviceVulkan12Features = .{
            .p_next = &features13,
            .buffer_device_address = .true,
            .descriptor_indexing = .true,
            .descriptor_binding_partially_bound = .true,
            .descriptor_binding_variable_descriptor_count = .true,
            .shader_sampled_image_array_non_uniform_indexing = .true,
            .descriptor_binding_sampled_image_update_after_bind = .true,
            .descriptor_binding_storage_image_update_after_bind = .true,
            .descriptor_binding_uniform_buffer_update_after_bind = .true,
            .runtime_descriptor_array = .true,
            .draw_indirect_count = .true,
            .timeline_semaphore = .true,
        };
        var features11: vk.PhysicalDeviceVulkan11Features = .{
            .p_next = &features12,
            .shader_draw_parameters = .true,
        };
        const features: vk.PhysicalDeviceFeatures2 = .{
            .p_next = &features11,
            .features = .{
                .multi_draw_indirect = .true,
                .independent_blend = .true,
            },
        };
        const dev = try inst.createDevice(pdev, &.{
            .p_next = &features,
            .queue_create_info_count = if (graphics_family == transport_family) 1 else 2,
            .p_queue_create_infos = &qci,
            .enabled_extension_count = required_device_extensions.len,
            .pp_enabled_extension_names = &required_device_extensions,
            .enabled_layer_count = 0,
            .pp_enabled_layer_names = undefined,
        }, null);

        const vkd = try alloc.create(DeviceWrapper);
        errdefer alloc.destroy(vkd);
        vkd.* = DeviceWrapper.load(dev, inst.wrapper.dispatch.vkGetDeviceProcAddr.?);
        const device = Device.init(dev, vkd);
        errdefer device.destroyDevice(null);

        const vma_vulkan_funcs = vma.VmaVulkanFunctions{
            .vkGetInstanceProcAddr = @ptrCast(vkb.dispatch.vkGetInstanceProcAddr.?),
            .vkGetDeviceProcAddr = @ptrCast(inst.wrapper.dispatch.vkGetDeviceProcAddr.?),
        };
        const vma_info = vma.VmaAllocatorCreateInfo{
            .physicalDevice = @ptrFromInt(@intFromEnum(pdev)),
            .device = @ptrFromInt(@intFromEnum(device.handle)),
            .instance = @ptrFromInt(@intFromEnum(inst.handle)),
            .flags = vma.VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .pVulkanFunctions = &vma_vulkan_funcs,
        };
        var vma_alloc: vma.VmaAllocator = undefined;
        const res = vma.vmaCreateAllocator(&vma_info, &vma_alloc);
        if (res < 0) return error.VmaError;

        return GraphicsCtx{
            .vkb = vkb,
            .instance = inst,
            .pdev = pdev,
            .props = props,
            .dev = device,
            .vma_alloc = vma_alloc,

            .graphics_family = graphics_family,
            .graphics_queue = device.getDeviceQueue(graphics_family, 0),

            .transport_family = transport_family,
            .transport_queue = device.getDeviceQueue(transport_family, 0),
            .has_dedicated_transport = has_dedicated_transport,

            .surface = surface,

            .debug_messenger = debug_messenger,
        };
    }

    pub fn deinit(self: GraphicsCtx, alloc: Allocator) void {
        var vma_stats: vma.VmaTotalStatistics = undefined;
        vma.vmaCalculateStatistics(self.vma_alloc, &vma_stats);
        if (vma_stats.total.statistics.allocationCount != 0) {
            var stats_ptr: [*c]u8 = undefined;
            vma.vmaBuildStatsString(self.vma_alloc, &stats_ptr, @intFromEnum(vk.Bool32.true));
            defer vma.vmaFreeStatsString(self.vma_alloc, stats_ptr);
            std.log.info("VMA stats:\n{s}", .{stats_ptr});
        }

        vma.vmaDestroyAllocator(self.vma_alloc);

        self.dev.destroyDevice(null);
        self.instance.destroySurfaceKHR(self.surface, null);
        self.instance.destroyDebugUtilsMessengerEXT(self.debug_messenger, null);
        self.instance.destroyInstance(null);

        alloc.destroy(self.dev.wrapper);
        alloc.destroy(self.instance.wrapper);
    }

    pub fn queueFamilyFromType(self: *const GraphicsCtx, t: QueueType) u32 {
        switch (t) {
            .graphics => return self.graphics_family,
            .transport => return self.transport_family,
        }
    }
};
