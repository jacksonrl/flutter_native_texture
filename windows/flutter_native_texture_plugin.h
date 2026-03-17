#ifndef FLUTTER_PLUGIN_flutter_native_texture_PLUGIN_H_
#define FLUTTER_PLUGIN_flutter_native_texture_PLUGIN_H_

#include <d3d11.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/texture_registrar.h>
#include <wrl/client.h>

#include <memory>

#include "../src/flutter_native_texture_api.h"

namespace flutter_native_texture {

struct GpuTextureObject {
    GpuTextureObject(int width, int height, flutter::TextureRegistrar& registrar, Microsoft::WRL::ComPtr<ID3D11Device> device);
    ~GpuTextureObject();

    int width, height;
    int64_t texture_id;
    flutter::TextureRegistrar& texture_registrar;

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;

    // Flutter display texture
    Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d_texture;

    // working texture
    Microsoft::WRL::ComPtr<ID3D11Texture2D> working_texture;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> display_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> working_srv;

    std::unique_ptr<flutter::TextureVariant> texture_variant;
    std::unique_ptr<FlutterDesktopGpuSurfaceDescriptor> surface_descriptor;
};

class FlutterNativeTexturePlugin {
   public:
    static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);
};

}  // namespace flutter_native_texture

#endif