#include "goliath/assets.hpp"
#include "goliath/scenes.hpp"

namespace engine::assets {
    struct SceneIterator {
        engine::Assets::SceneHandle handle;
        engine::scenes::Iterator it;
    };
}

namespace engine {
    Assets Assets::init(Inputs& in) {
        Assets a{};

        for (size_t i = 0; i < in.scenes_size; i++) {
            a.names.emplace_back(in.scenes[i].name);
            a.ref_counts.emplace_back(0);
            a.in_draw.emplace_back(false);

            a.scene_gids.emplace_back(-1);

            *in.scenes[i].handle = {a.names.size() - 1};
        }
        a.models_start = a.names.size();

        for (size_t i = 0; i < in.models_size; i++) {
            a.names.emplace_back(in.models[i].name);
            a.ref_counts.emplace_back(0);
            a.in_draw.emplace_back(false);

            a.model_gids.emplace_back(models::gid{});

            *in.models[i].handle = {a.names.size() - 1};
        }
        a.textures_start = a.names.size();

        for (size_t i = 0; i < in.textures_size; i++) {
            a.names.emplace_back(in.textures[i].name);
            a.ref_counts.emplace_back(0);
            a.in_draw.emplace_back(false);

            a.texture_gids.emplace_back(textures::gid{});

            *in.textures[i].handle = {a.names.size() - 1};
        }

        in.destroy();

        return a;
    }

    void Assets::destroy() {
        for (size_t i = 0; i < models_start; i++) {
            auto rc = ref_counts[i];
            while (rc-- != 0) {
                scenes::release(scene_gids[i]);
            }
        }

        for (size_t i = models_start; i < textures_start; i++) {
            auto rc = ref_counts[i];
            while (rc-- != 0) {
                models::release(&model_gids[i - models_start], 1);
            }
        }

        for (size_t i = textures_start; i < ref_counts.size(); i++) {
            auto rc = ref_counts[i];
            while (rc-- != 0) {
                textures::release(&texture_gids[i - textures_start], 1);
            }
        }
    }

    void Assets::load(const nlohmann::json& j) {
        for (const auto& entry : j["scenes"]) {
            const std::string& str = entry["name"];
            auto last = names.begin() + models_start;
            auto it = std::find(names.begin(), last, str);
            if (it == last) continue;

            auto ix = std::distance(names.begin(), it);
            scene_gids[ix] = entry["gid"];
        }

        for (const auto& entry : j["models"]) {
            const std::string& str = entry["name"];
            auto start = names.begin() + models_start;
            auto last = names.begin() + textures_start;
            auto it = std::find(start, last, str);
            if (it == last) continue;

            auto ix = std::distance(start, it);
            model_gids[ix] = entry["gid"];
        }

        for (const auto& entry : j["textures"]) {
            const std::string& str = entry["name"];
            auto start = names.begin() + textures_start;
            auto last = names.end();
            auto it = std::find(start, last, str);
            if (it == last) continue;

            auto ix = std::distance(start, it);
            texture_gids[ix] = entry["gid"];
        }
    }

    nlohmann::json Assets::save() {
        auto scenes = nlohmann::json::array();
        auto models = nlohmann::json::array();
        auto textures = nlohmann::json::array();

        for (size_t i = 0; i < models_start; i++) {
            scenes.emplace_back(nlohmann::json{
                {"name", names[i]},
                {"gid", scene_gids[i]},
            });
        }

        for (size_t i = models_start; i < textures_start; i++) {
            models.emplace_back(nlohmann::json{
                {"name", names[i]},
                {"gid", model_gids[i - models_start]},
            });
        }

        for (size_t i = textures_start; i < names.size(); i++) {
            textures.emplace_back(nlohmann::json{
                {"name", names[i]},
                {"gid", texture_gids[i - textures_start]},
            });
        }

        return {
            {"scenes", scenes},
            {"models", models},
            {"textures", textures},
        };
    }

    nlohmann::json Assets::default_json() {
        return {
            {"scenes", nlohmann::json::array()},
            {"models", nlohmann::json::array()},
            {"textures", nlohmann::json::array()},
        };
    }

    void Assets::acquire(SceneHandle handle) {
        scenes::acquire(scene_gids[handle.n]);
        ref_counts[handle.n]++;
    }

    void Assets::acquire(ModelHandle handle) {
        models::acquire(&model_gids[handle.n], 1);
        ref_counts[models_start + handle.n]++;
    }

    void Assets::acquire(TextureHandle handle) {
        textures::acquire(&texture_gids[handle.n], 1);
        ref_counts[textures_start + handle.n]++;
    }

    void Assets::release(SceneHandle handle) {
        auto ix = handle.n;
        scenes::release(scene_gids[handle.n]);

        assert(ref_counts[ix] != 0);
        ref_counts[ix]--;
    }

    void Assets::release(ModelHandle handle) {
        auto ix = models_start + handle.n;
        models::release(&model_gids[handle.n], 1);

        assert(ref_counts[ix] != 0);
        ref_counts[ix]--;
    }

    void Assets::release(TextureHandle handle) {
        auto ix = textures_start + handle.n;
        textures::acquire(&texture_gids[handle.n], 1);

        assert(ref_counts[ix] != 0);
        ref_counts[ix]--;
    }

    Assets::ModelDraw Assets::draw_model(ModelHandle handle) {
        auto ix = models_start + handle.n;
        auto gid = model_gids[handle.n];
        assert(!in_draw[ix]);
        if (gid == models::gid{}) {
            assert(false && "TODO: throw an editor error");
        }
        in_draw[ix] = true;

        return ModelDraw{
            .gid = gid,
        };
    }

    void Assets::end_model_draw(ModelHandle handle) {
        auto ix = models_start + handle.n;
        assert(in_draw[ix]);
        in_draw[ix] = false;
    }

    Assets::TextureDraw Assets::draw_texture(TextureHandle handle) {
        auto ix = textures_start + handle.n;
        auto gid = texture_gids[handle.n];
        assert(!in_draw[ix]);
        if (gid == textures::gid{}) {
            assert(false && "TODO: throw an editor error");
        }
        in_draw[ix] = true;

        return TextureDraw{
            .gid = gid,
        };
    }

    void Assets::end_texture_draw(TextureHandle handle) {
        auto ix = textures_start + handle.n;
        assert(in_draw[ix]);
        in_draw[ix] = false;
    }

    assets::SceneIterator* Assets::draw_scene(SceneHandle handle, transport2::ticket& t, uint64_t& transforms_addr) {
        auto ix = handle.n;
        auto gid = scene_gids[handle.n];
        assert(!in_draw[ix]);
        in_draw[ix] = true;
        if (gid == -1) {
            assert(false && "TODO: throw an editor error");
        }

        transforms_addr = scenes::get_instance_transforms_buffer(gid, t).address();
        return new assets::SceneIterator{
            .handle = handle,
            .it = scenes::draw(gid, t, transforms_addr),
        };
    }

    scenes::Draw Assets::scene_next_model(assets::SceneIterator* it) {
        return it->it.next();
    }

    void Assets::end_scene_draw(assets::SceneIterator* it) {
        auto ix = it->handle.n;
        assert(in_draw[ix]);
        in_draw[ix] = false;

        delete it;
    }

    void Assets::set(size_t ix, size_t gid) {
        assert(!in_draw[ix]);

        auto old_gid = scene_gids[ix];
        auto rc = ref_counts[ix];
        while (rc-- != 0) {
            scenes::release(old_gid);
            scenes::acquire(gid);
        }

        scene_gids[ix] = gid;
    }

    void Assets::set(size_t ix, models::gid gid) {
        auto gix = ix + models_start;
        assert(!in_draw[gix]);

        auto old_gid = model_gids[ix];
        auto rc = ref_counts[gix];
        while (rc-- != 0) {
            models::release(&old_gid, 1);
            models::acquire(&gid, 1);
        }

        model_gids[ix] = gid;
    }

    void Assets::set(size_t ix, textures::gid gid) {
        auto gix = ix + textures_start;
        assert(!in_draw[gix]);

        auto old_gid = texture_gids[ix];
        auto rc = ref_counts[gix];
        while (rc-- != 0) {
            textures::release(&old_gid, 1);
            textures::acquire(&gid, 1);
        }

        texture_gids[ix] = gid;
    }

    std::span<const std::string> Assets::get_scene_names() const {
        return {names.data(), models_start};
    }

    std::span<const std::string> Assets::get_model_names() const {
        return {names.data() + models_start, textures_start};
    }

    std::span<const std::string> Assets::get_texture_names() const {
        return {names.data() + textures_start, names.size()};
    }

    std::span<const size_t> Assets::get_scene_gids() const {
        return scene_gids;
    }

    std::span<const models::gid> Assets::get_model_gids() const {
        return model_gids;
    }

    std::span<const textures::gid> Assets::get_texture_gids() const {
        return texture_gids;
    }

    bool Assets::want_to_save() {
        auto res = want_save;
        want_save = false;
        return res;
    }
}
