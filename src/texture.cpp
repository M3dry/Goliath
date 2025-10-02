#include "goliath/texture.hpp"
#include "goliath/engine.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION

#include <cstddef>
#include <volk.h>

namespace engine {
    class Image {
      public:
        enum Type {
            _8,
            _16,
            _32,
        };

        void* data = nullptr;
        std::size_t size = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t components = 0;
        Type type;
        VkFormat format;

        static Image load8(const char* filename) {
            Image image;
            image.type = _8;
            image.data = stbi_load(filename, (int*)&image.width, (int*)&image.height, (int*)&image.components, 0);
            image.calculate_size();
            image.get_format();
            return image;
        }

        static Image load8(uint8_t* mem, uint32_t size) {
            Image image;
            image.type = _8;
            image.data = stbi_load_from_memory(mem, (int)size, (int*)&image.width, (int*)&image.height,
                                               (int*)&image.components, 0);
            image.calculate_size();
            image.get_format();
            return image;
        }

        static Image load16(const char* filename) {
            Image image;
            image.type = _16;
            image.data = stbi_load_16(filename, (int*)&image.width, (int*)&image.height, (int*)&image.components, 0);
            image.calculate_size();
            image.get_format();
            return image;
        }

        static Image load16(uint8_t* mem, uint32_t size) {
            Image image;
            image.type = _16;
            image.data = stbi_load_16_from_memory(mem, (int)size, (int*)&image.width, (int*)&image.height,
                                                  (int*)&image.components, 0);
            image.calculate_size();
            image.get_format();
            return image;
        }

      private:
        void calculate_size() {
            size = components * width * height * (type == _8 ? 1 : 2);
        }

        void get_format() {
            if (type == _8) {
                switch (components) {
                    case 1:
                        format = VK_FORMAT_R8_UNORM;
                        break;
                    case 2:
                        format = VK_FORMAT_R8G8_UNORM;
                        break;
                    case 3:
                        format = VK_FORMAT_R8G8B8_UNORM;
                        break;
                    case 4:
                        format = VK_FORMAT_R8G8B8A8_UNORM;
                        break;
                }
            } else if (type == _16) {
                switch (components) {
                    case 1:
                        format = VK_FORMAT_R16_UNORM;
                        break;
                    case 2:
                        format = VK_FORMAT_R16G16_UNORM;
                        break;
                    case 3:
                        format = VK_FORMAT_R16G16B16_UNORM;
                        break;
                    case 4:
                        format = VK_FORMAT_R16G16B16A16_UNORM;
                        break;
                }
            }
        }
    };

    struct GPUImage {
        VkImage image;
        VmaAllocation allocation;

        static GPUImage load(const Image& img) {
            GPUImage gpu_img;

            VkImageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.pNext = nullptr;
            info.imageType = VK_IMAGE_TYPE_2D;
            info.arrayLayers = 1;
            info.extent = VkExtent3D{
                .width = img.width,
                .height = img.height,
                .depth = 1,
            };
            info.format = img.format;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            info.mipLevels = 1;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.samples = VK_SAMPLE_COUNT_1_BIT;

            VmaAllocationCreateInfo alloc_info{};
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

            VK_CHECK(vmaCreateImage(allocator, &info, &alloc_info, &gpu_img.image, &gpu_img.allocation, nullptr));

            return gpu_img;
        }
    };

    struct GPUImageView {};

    struct Sampler {};

    struct Texture {
        GPUImageView view;
        Sampler sampler;
        uint32_t texutre_pool_id = (uint32_t)-1;
    };
}
