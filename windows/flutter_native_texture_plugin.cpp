#include "flutter_native_texture_plugin.h"

#include <d3dcompiler.h>

#include <iostream>
#include <map>
#include <mutex>
#include <vector>

using namespace flutter_native_texture;
using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> g_d3d_device;
static ComPtr<ID3D11DeviceContext> g_d3d_context;
static ComPtr<ID3D11VertexShader> g_blit_vs;
static ComPtr<ID3D11PixelShader> g_blit_ps;
static ComPtr<ID3D11SamplerState> g_sampler;

static ComPtr<ID3D11RasterizerState> g_rasterizer_state;
static ComPtr<ID3D11BlendState> g_blend_state;
static ComPtr<ID3D11DepthStencilState> g_depth_state;

static flutter::TextureRegistrar* g_texture_registrar = nullptr;
static std::map<FlutterNativeTextureTexture, std::unique_ptr<GpuTextureObject>> g_textures;
static std::mutex g_mutex;

const char* kBlitShader = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct VS_OUT { 
    float4 pos : SV_POSITION; 
    float2 uv : TEXCOORD; 
};

VS_OUT vs_main(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 ps_main(VS_OUT i) : SV_Target {
    return tex.Sample(samp, i.uv);
}
)";

bool InitializeShaders() {
    if (g_blit_vs) return true;
    ComPtr<ID3DBlob> vs_blob, ps_blob, err_blob;
    if (FAILED(D3DCompile(kBlitShader, strlen(kBlitShader), nullptr, nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs_blob, &err_blob))) return false;
    g_d3d_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_blit_vs);
    if (FAILED(D3DCompile(kBlitShader, strlen(kBlitShader), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps_blob, &err_blob))) return false;
    g_d3d_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_blit_ps);

    D3D11_SAMPLER_DESC samp_desc = {};
    samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp_desc.AddressU = samp_desc.AddressV = samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.MaxLOD = D3D11_FLOAT32_MAX;
    g_d3d_device->CreateSamplerState(&samp_desc, &g_sampler);

    D3D11_RASTERIZER_DESC rs_desc = {};
    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    g_d3d_device->CreateRasterizerState(&rs_desc, &g_rasterizer_state);

    D3D11_BLEND_DESC bl_desc = {};
    bl_desc.RenderTarget[0].BlendEnable = FALSE;
    bl_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_d3d_device->CreateBlendState(&bl_desc, &g_blend_state);

    D3D11_DEPTH_STENCIL_DESC ds_desc = {};
    ds_desc.DepthEnable = FALSE;
    ds_desc.StencilEnable = FALSE;
    g_d3d_device->CreateDepthStencilState(&ds_desc, &g_depth_state);

    return true;
}

bool InitializeD3D11() {
    if (g_d3d_device) return true;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1, D3D11_SDK_VERSION, &g_d3d_device, nullptr, &g_d3d_context))) return false;
    return InitializeShaders();
}

GpuTextureObject::GpuTextureObject(int w, int h, flutter::TextureRegistrar& registrar, ComPtr<ID3D11Device> device)
    : width(w), height(h), texture_registrar(registrar), d3d_device(device) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    d3d_device->CreateTexture2D(&desc, nullptr, &d3d_texture);

    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    d3d_device->CreateTexture2D(&desc, nullptr, &working_texture);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d3d_device->CreateTexture2D(&desc, nullptr, &staging_texture);

    d3d_device->CreateRenderTargetView(d3d_texture.Get(), nullptr, &display_rtv);
    d3d_device->CreateShaderResourceView(working_texture.Get(), nullptr, &working_srv);

    ComPtr<IDXGIResource> dxgi_resource;
    d3d_texture.As(&dxgi_resource);
    HANDLE shared_handle;
    dxgi_resource->GetSharedHandle(&shared_handle);

    surface_descriptor = std::make_unique<FlutterDesktopGpuSurfaceDescriptor>();
    surface_descriptor->struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
    surface_descriptor->handle = shared_handle;
    surface_descriptor->width = w;
    surface_descriptor->height = h;
    surface_descriptor->visible_width = w;
    surface_descriptor->visible_height = h;
    surface_descriptor->format = kFlutterDesktopPixelFormatBGRA8888;

    texture_variant = std::make_unique<flutter::TextureVariant>(flutter::GpuSurfaceTexture(
        kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle, [this](size_t w, size_t h) { return this->surface_descriptor.get(); }));
    texture_id = texture_registrar.RegisterTexture(texture_variant.get());
}

GpuTextureObject::~GpuTextureObject() {
    texture_registrar.UnregisterTexture(texture_id);
}

void FlutterNativeTexturePlugin::RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar) {
    g_texture_registrar = registrar->texture_registrar();
    InitializeD3D11();
}

extern "C" {
API_EXPORT FlutterNativeTextureTexture flutter_native_texture_create_texture(int32_t width, int32_t height) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!InitializeD3D11()) return nullptr;
    auto tex = std::make_unique<GpuTextureObject>(width, height, *g_texture_registrar, g_d3d_device);
    FlutterNativeTextureTexture handle = (FlutterNativeTextureTexture)tex.get();
    g_textures[handle] = std::move(tex);
    return handle;
}

API_EXPORT int64_t flutter_native_texture_get_texture_id(FlutterNativeTextureTexture t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_textures.count(t) ? g_textures.at(t)->texture_id : -1;
}

API_EXPORT void flutter_native_texture_upload_texture(FlutterNativeTextureTexture t, const uint8_t* data, int32_t x, int32_t y, int32_t w, int32_t h) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_textures.count(t)) return;
    auto& tex = g_textures.at(t);
    D3D11_BOX box = {(UINT)x, (UINT)y, 0, (UINT)(x + w), (UINT)(y + h), 1};
    g_d3d_context->UpdateSubresource(tex->working_texture.Get(), 0, &box, data, w * 4, 0);
}

API_EXPORT void flutter_native_texture_present_texture(FlutterNativeTextureTexture t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_textures.count(t)) return;
    auto& tex = g_textures.at(t);

    float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    g_d3d_context->OMSetBlendState(g_blend_state.Get(), blendFactor, 0xffffffff);
    g_d3d_context->OMSetDepthStencilState(g_depth_state.Get(), 0);
    g_d3d_context->RSSetState(g_rasterizer_state.Get());
    g_d3d_context->OMSetRenderTargets(1, tex->display_rtv.GetAddressOf(), nullptr);

    float clear[] = {0, 0, 0, 0};
    g_d3d_context->ClearRenderTargetView(tex->display_rtv.Get(), clear);

    D3D11_VIEWPORT vp = {0, 0, (float)tex->width, (float)tex->height, 0, 1};
    g_d3d_context->RSSetViewports(1, &vp);
    g_d3d_context->IASetInputLayout(nullptr);
    g_d3d_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_d3d_context->VSSetShader(g_blit_vs.Get(), nullptr, 0);
    g_d3d_context->PSSetShader(g_blit_ps.Get(), nullptr, 0);
    g_d3d_context->PSSetShaderResources(0, 1, tex->working_srv.GetAddressOf());
    g_d3d_context->PSSetSamplers(0, 1, g_sampler.GetAddressOf());

    g_d3d_context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    g_d3d_context->PSSetShaderResources(0, 1, &nullSRV);
    g_d3d_context->Flush();
    tex->texture_registrar.MarkTextureFrameAvailable(tex->texture_id);
}

// has not been tested...
API_EXPORT void flutter_native_texture_download_texture(FlutterNativeTextureTexture t, uint8_t* out_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_textures.count(t)) return;
    auto& tex = g_textures.at(t);
    g_d3d_context->CopyResource(tex->staging_texture.Get(), tex->working_texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_d3d_context->Map(tex->staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        for (int i = 0; i < tex->height; i++) memcpy(out_data + (i * tex->width * 4), (uint8_t*)mapped.pData + (i * mapped.RowPitch), tex->width * 4);
        g_d3d_context->Unmap(tex->staging_texture.Get(), 0);
    }
}

API_EXPORT void* flutter_native_texture_get_hardware_buffer(FlutterNativeTextureTexture t) {
    return nullptr;
}

API_EXPORT void* flutter_native_texture_get_dxgi_shared_handle(FlutterNativeTextureTexture ref) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto* tex = static_cast<GpuTextureObject*>(ref);
    if (!tex) return nullptr;

    Microsoft::WRL::ComPtr<IDXGIResource> dxgi_resource;
    // HRESULT hr = tex->d3d_texture.As(&dxgi_resource);
    HRESULT hr = tex->working_texture.As(&dxgi_resource);
    if (FAILED(hr)) return nullptr;

    HANDLE shared_handle = nullptr;
    hr = dxgi_resource->GetSharedHandle(&shared_handle);
    if (FAILED(hr)) return nullptr;

    return (void*)shared_handle;
}

API_EXPORT void* flutter_native_texture_get_android_native_window(FlutterNativeTextureTexture ref) {
    return nullptr;
}

API_EXPORT void flutter_native_texture_dispose_texture(FlutterNativeTextureTexture t) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_textures.erase(t);
}
}