#ifndef flutter_native_texture_API_H
#define flutter_native_texture_API_H

#include <stdint.h>

#if _WIN32
#define API_EXPORT __declspec(dllexport)
#else
#define API_EXPORT
#endif

typedef void* FlutterNativeTextureTexture;

#ifdef __cplusplus
extern "C" {
#endif

// High Level API
API_EXPORT FlutterNativeTextureTexture flutter_native_texture_create_texture(int32_t width, int32_t height);
API_EXPORT int64_t flutter_native_texture_get_texture_id(FlutterNativeTextureTexture handle);
API_EXPORT void flutter_native_texture_upload_texture(FlutterNativeTextureTexture handle, const uint8_t* data, int32_t x, int32_t y, int32_t w, int32_t h);
API_EXPORT void flutter_native_texture_download_texture(FlutterNativeTextureTexture handle, uint8_t* out_data);
API_EXPORT void flutter_native_texture_present_texture(FlutterNativeTextureTexture handle);
API_EXPORT void flutter_native_texture_dispose_texture(FlutterNativeTextureTexture handle);

// Platform Specific Handle Extractors

// Returns HANDLE on Windows, nullptr on others
API_EXPORT void* flutter_native_texture_get_dxgi_shared_handle(FlutterNativeTextureTexture ref);

// Returns ANativeWindow* on Android, nullptr on others
API_EXPORT void* flutter_native_texture_get_android_native_window(FlutterNativeTextureTexture ref);

// Returns AHardwareBuffer* on Android, nullptr on others
API_EXPORT void* flutter_native_texture_get_hardware_buffer(FlutterNativeTextureTexture ref);

#ifdef __cplusplus
}
#endif

#endif