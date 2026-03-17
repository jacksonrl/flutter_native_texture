#include "flutter_native_texture_ffi.h"

#include <flutter/plugin_registrar_windows.h>

#include <map>
#include <memory>

#include "flutter_native_texture_plugin.h"

static flutter::PluginRegistrarWindows* g_registrar = nullptr;
static std::map<int64_t, std::unique_ptr<flutter_native_texture::PixelTextureObject>> g_textures;

void flutter_native_texture_init_ffi(void* registrar) {
    g_registrar = static_cast<flutter::PluginRegistrarWindows*>(registrar);
}

int64_t flutter_native_texture_create_texture(int32_t width, int32_t height) {
    if (!g_registrar || !g_registrar->texture_registrar()) {
        return -1;  // Not initialized
    }

    auto texture_object = std::make_unique<flutter_native_texture::PixelTextureObject>(
        width, height, *g_registrar->texture_registrar());

    int64_t texture_id = texture_object->get_texture_id();
    g_textures[texture_id] = std::move(texture_object);

    return texture_id;
}

uint8_t* flutter_native_texture_get_pixel_buffer(int64_t texture_id) {
    auto it = g_textures.find(texture_id);
    if (it != g_textures.end()) {
        return it->second->get_pixels().data();
    }
    return nullptr;
}

void flutter_native_texture_invalidate_texture(int64_t texture_id) {
    if (!g_registrar) return;

    auto it = g_textures.find(texture_id);
    if (it != g_textures.end()) {
        it->second->invalidate();
    }
}

void flutter_native_texture_dispose_texture(int64_t texture_id) {
    auto it = g_textures.find(texture_id);
    if (it != g_textures.end()) {
        g_textures.erase(it);
    }
}