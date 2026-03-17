# FlutterNativeTexture

Flutter Plugin that makes working with native textures easier. Provides a high level, cross platform API to create and upload data from the CPU to the GPU that can be displayed with a texture widget, as well as a set of low level APIs to work directly with shared textures for usage in other libraries. Currently supports Windows and Android

# Full Example

## High Level API

```dart

import 'dart:ffi';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:flutter_native_texture/flutter_native_texture.dart';

void main() {
  runApp(const MaterialApp(
    home: NativeTextureExample(),
    debugShowCheckedModeBanner: false,
  ));
}

class NativeTextureExample extends StatefulWidget {
  const NativeTextureExample({super.key});

  @override
  State<NativeTextureExample> createState() => _NativeTextureExampleState();
}

class _NativeTextureExampleState extends State<NativeTextureExample> {
  Pointer<Void>? _textureHandle;
  int _textureId = -1;
  final int _size = 512;
  final int _rectSize = 200;

  @override
  void initState() {
    super.initState();
    _setupTexture();
  }

  void _setupTexture() {
    final rend = FlutterNativeTexture.instance;
    _textureHandle = rend.createTexture(_size, _size);
    _textureId = rend.getTextureId(_textureHandle!);

    final Uint8List pixels = Uint8List(_rectSize * _rectSize * 4);
    for (int i = 0; i < pixels.length; i += 4) {
      pixels[i] = 255;     //red
      pixels[i + 1] = 0;   
      pixels[i + 2] = 0;   
      pixels[i + 3] = 255; 
    }

    int offset = (_size - _rectSize) ~/ 2;
    rend.uploadRect(_textureHandle!, pixels, offset, offset, _rectSize, _rectSize);

    // call present to push data to the display texture
    rend.present(_textureHandle!);

    setState(() {});
  }

  @override
  void dispose() {
    if (_textureHandle != null) {
      FlutterNativeTexture.instance.disposeTexture(_textureHandle!);
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: Center(
        child: _textureId == -1
            ? const CircularProgressIndicator()
            : Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Text(
                    "Hello Red",
                    style: TextStyle(color: Colors.white, fontSize: 16),
                  ),
                  const SizedBox(height: 20),
                  Container(
                    width: _size.toDouble(),
                    height: _size.toDouble(),
                    decoration: BoxDecoration(
                      border: Border.all(color: Colors.white24, width: 1),
                    ),
                    child: Texture(textureId: _textureId),
                  ),
                  const SizedBox(height: 20),
                  ElevatedButton(
                    onPressed: () {
                      // Example of a partial update: Draw a smaller blue square at the top left
                      final bluePixels = Uint8List(50 * 50 * 4);
                      for (int i = 0; i < bluePixels.length; i += 4) {
                        bluePixels[i] = 255;
                        bluePixels[i + 1] = 0;
                        bluePixels[i + 2] = 255; // blue
                        bluePixels[i + 3] = 255; // Alpha
                      }
                      FlutterNativeTexture.instance.uploadRect(_textureHandle!, bluePixels, 0, 0, 50, 50);
                      FlutterNativeTexture.instance.present(_textureHandle!);
                    },
                    child: const Text("Draw Blue Corner (Partial Update)"),
                  )
                ],
              ),
      ),
    );
  }
}

```

## Low-Level API

Here is how one can render dawn into a flutter texture using this pacakge:

```dart

import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';
import 'package:webgpu_rend/src/webgpu_bindings_generated.dart';
import 'package:flutter_native_texture/flutter_native_texture.dart';

export 'package:webgpu_rend/src/webgpu_bindings_generated.dart';

class GpuTexture {
  final WebGpuBindings _bindings;
  final Pointer<Void> flutterHandle; 
  final int textureId;               
  final WGPUTexture texture;         
  final WGPUSharedTextureMemory stm; 

  int _vkLayout = 0;

  GpuTexture(this._bindings, this.flutterHandle, this.textureId, this.texture, this.stm);

  void beginAccess() {
    using((arena) {
      final desc = arena<WGPUSharedTextureMemoryBeginAccessDescriptor>();
      desc.ref.initialized = 1; 
      desc.ref.fenceCount = 0;

      if (Platform.isAndroid) {
        final vkBegin = arena<WGPUSharedTextureMemoryVkImageLayoutBeginState>();
        vkBegin.ref.chain.sType = WGPUSType.WGPUSType_SharedTextureMemoryVkImageLayoutBeginState;
        vkBegin.ref.oldLayout = _vkLayout;
        vkBegin.ref.newLayout = 0;
        desc.ref.nextInChain = vkBegin.cast();
      }

      _bindings.wgpuSharedTextureMemoryBeginAccess(stm, texture, desc);
    });
  }

  void endAccess() {
    using((arena) {
      final state = arena<WGPUSharedTextureMemoryEndAccessState>();

      Pointer<WGPUSharedTextureMemoryVkImageLayoutEndState> vkEnd = nullptr;

      if (Platform.isAndroid) {
        vkEnd = arena<WGPUSharedTextureMemoryVkImageLayoutEndState>();
        vkEnd.ref.chain.sType = WGPUSType.WGPUSType_SharedTextureMemoryVkImageLayoutEndState;
        state.ref.nextInChain = vkEnd.cast();
      }

      _bindings.wgpuSharedTextureMemoryEndAccess(stm, texture, state);

      if (Platform.isAndroid && vkEnd != nullptr) {
        _vkLayout = vkEnd.ref.oldLayout;
      }
    });
  }

  void present() {
    endAccess(); 
    FlutterNativeTexture.instance.present(flutterHandle);
  }

  void dispose() {
    _bindings.wgpuTextureRelease(texture);
    _bindings.wgpuSharedTextureMemoryRelease(stm);
    FlutterNativeTexture.instance.disposeTexture(flutterHandle);
  }
}

void _uncapturedErrorCallback(
  Pointer<WGPUDevice> device,
  int type,
  WGPUStringView message,
  Pointer<Void> userdata1,
  Pointer<Void> userdata2,
) {
  try {      
    final str = message.data.cast<Utf8>().toDartString(length: message.length);
    print('WebGPU Error ($type): $str');
  } catch (e) {
    print('WebGPU Error: Could not decode error message.');
  }
}

class WebGpuEngine {
  late final DynamicLibrary dylib;
  late final WebGpuBindings bindings;
  late final WGPUInstance instance;
  late final WGPUDevice device;
  late final WGPUQueue queue;

  late final Pointer<Void> Function(Pointer<Char>) _getProcAddress;

  WebGpuEngine() {
    dylib = Platform.isWindows 
        ? DynamicLibrary.open('webgpu_rend_plugin.dll')
        : DynamicLibrary.open('libwebgpu_rend_android.so');

    _getProcAddress = dylib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Char>)>>(
            'webgpu_rend_get_proc_address')
        .asFunction();
    
    bindings = WebGpuBindings.fromLookup(_lookupProc);
  }

  Pointer<T> _lookupProc<T extends NativeType>(String symbolName) {
    if (dylib.providesSymbol(symbolName)) return dylib.lookup(symbolName);
    final namePtr = symbolName.toNativeUtf8();
    final ptr = _getProcAddress(namePtr.cast());
    malloc.free(namePtr);
    if (ptr == nullptr) throw "Symbol not found: $symbolName";
    return ptr.cast();
  }

  Future<void> initialize() async {
    final instanceDesc = calloc<WGPUInstanceDescriptor>();
    instance = bindings.wgpuCreateInstance(instanceDesc);
    calloc.free(instanceDesc);

    device = await _requestDevice(instance);
    queue = bindings.wgpuDeviceGetQueue(device);
  }

  Future<WGPUDevice> _requestDevice(WGPUInstance instance) async {
    final adapterCompleter = Completer<WGPUAdapter>();

    final onAdapter = NativeCallable<WGPURequestAdapterCallbackFunction>.listener(
      (int status, WGPUAdapter adapter, WGPUStringView message, Pointer<Void> u1, Pointer<Void> u2) {
        if (status == WGPURequestAdapterStatus.WGPURequestAdapterStatus_Success.value) {
          adapterCompleter.complete(adapter);
        } else {
          adapterCompleter.completeError("Failed to get adapter: $status");
        }
    });

    final options = calloc<WGPURequestAdapterOptions>();
    options.ref.powerPreference = WGPUPowerPreference.WGPUPowerPreference_HighPerformance;
    
    final adapterInfo = calloc<WGPURequestAdapterCallbackInfo>();
    adapterInfo.ref.callback = onAdapter.nativeFunction;
    adapterInfo.ref.mode = WGPUCallbackMode.WGPUCallbackMode_AllowSpontaneous;
    
    bindings.wgpuInstanceRequestAdapter(instance, options, adapterInfo.ref);
    final adapter = await adapterCompleter.future;
    onAdapter.close();

    final deviceCompleter = Completer<WGPUDevice>();
    final onDevice = NativeCallable<WGPURequestDeviceCallbackFunction>.listener(
      (int status, WGPUDevice device, WGPUStringView message, Pointer<Void> u1, Pointer<Void> u2) {
        if (status == WGPURequestDeviceStatus.WGPURequestDeviceStatus_Success.value) {
          deviceCompleter.complete(device);
        } else {
          deviceCompleter.completeError("Failed to get device: $status");
        }
    });

    final onDeviceError = NativeCallable<WGPUUncapturedErrorCallbackFunction>.isolateLocal(_uncapturedErrorCallback);
    final errorCallbackInfo = calloc<WGPUUncapturedErrorCallbackInfo>();
    errorCallbackInfo.ref.callback = onDeviceError.nativeFunction;

    final deviceDesc = calloc<WGPUDeviceDescriptor>();
    deviceDesc.ref.uncapturedErrorCallbackInfo = errorCallbackInfo.ref;

    final requiredFeatures = calloc<Int32>(1);
    if (Platform.isWindows) {
      requiredFeatures[0] = WGPUFeatureName.WGPUFeatureName_SharedTextureMemoryDXGISharedHandle.value;
    } else {
      requiredFeatures[0] = WGPUFeatureName.WGPUFeatureName_SharedTextureMemoryAHardwareBuffer.value;
    }
    deviceDesc.ref.requiredFeatureCount = 1;
    deviceDesc.ref.requiredFeatures = requiredFeatures.cast<UnsignedInt>();

    final deviceCBInfo = calloc<WGPURequestDeviceCallbackInfo>();
    deviceCBInfo.ref.callback = onDevice.nativeFunction;
    deviceCBInfo.ref.mode = WGPUCallbackMode.WGPUCallbackMode_AllowSpontaneous;

    bindings.wgpuAdapterRequestDevice(adapter, deviceDesc, deviceCBInfo.ref);

    final deviceResult = await deviceCompleter.future;
    onDevice.close();
    return deviceResult;
  }

  GpuTexture createTexture(int width, int height) {
    final plugin = FlutterNativeTexture.instance;
    
    final flutterHandle = plugin.createTexture(width, height);
    final textureId = plugin.getTextureId(flutterHandle);

    return using((arena) {
      WGPUSharedTextureMemory stm;

      if (Platform.isWindows) {
        final dxgiHandle = plugin.getDxgiSharedHandle(flutterHandle);
        final dxgiDesc = arena<WGPUSharedTextureMemoryDXGISharedHandleDescriptor>();
        dxgiDesc.ref.chain.sType = WGPUSType.WGPUSType_SharedTextureMemoryDXGISharedHandleDescriptor;
        dxgiDesc.ref.handle = dxgiHandle;

        final stmDesc = arena<WGPUSharedTextureMemoryDescriptor>();
        stmDesc.ref.nextInChain = dxgiDesc.cast();
        stm = bindings.wgpuDeviceImportSharedTextureMemory(device, stmDesc);
      } else {
        final hbHandle = plugin.getAHardwareBuffer(flutterHandle);
        final hbDesc = arena<WGPUSharedTextureMemoryAHardwareBufferDescriptor>();
        hbDesc.ref.chain.sType = WGPUSType.WGPUSType_SharedTextureMemoryAHardwareBufferDescriptor;
        hbDesc.ref.handle = hbHandle;

        final stmDesc = arena<WGPUSharedTextureMemoryDescriptor>();
        stmDesc.ref.nextInChain = hbDesc.cast();
        stm = bindings.wgpuDeviceImportSharedTextureMemory(device, stmDesc);
      }

      final texDesc = arena<WGPUTextureDescriptor>();
      texDesc.ref.dimension = WGPUTextureDimension.WGPUTextureDimension_2D;
      texDesc.ref.size.width = width;
      texDesc.ref.size.height = height;
      texDesc.ref.size.depthOrArrayLayers = 1;
      texDesc.ref.format = WGPUTextureFormat.WGPUTextureFormat_RGBA8Unorm;
          
      texDesc.ref.usage = WGPUTextureUsage_RenderAttachment | 
                          WGPUTextureUsage_CopySrc | 
                          WGPUTextureUsage_TextureBinding;
      texDesc.ref.mipLevelCount = 1;
      texDesc.ref.sampleCount = 1;

      final texture = bindings.wgpuSharedTextureMemoryCreateTexture(stm, texDesc);

      return GpuTexture(bindings, flutterHandle, textureId, texture, stm);
    });
  }
}

```


Which could then be used like so:

```dart

import 'dart:async';
import 'dart:ffi';

import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:webgpu_rend/webgpu_test.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  
  final engine = WebGpuEngine();
  await engine.initialize();

  runApp(MaterialApp(
    home: RawWebGpuTriangle(engine: engine),
    debugShowCheckedModeBanner: false,
  ));
}

const String kShaderWgsl = r'''
@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4<f32> {
    var pos = array<vec2<f32>, 3>(
        vec2<f32>( 0.0,  0.5),
        vec2<f32>(-0.5, -0.5),
        vec2<f32>( 0.5, -0.5)
    );
    return vec4<f32>(pos[in_vertex_index], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0, 0.2, 0.3, 1.0); // Reddish triangle
}
''';

class RawWebGpuTriangle extends StatefulWidget {
  final WebGpuEngine engine;
  const RawWebGpuTriangle({super.key, required this.engine});

  @override
  State<RawWebGpuTriangle> createState() => _RawWebGpuTriangleState();
}

class _RawWebGpuTriangleState extends State<RawWebGpuTriangle> with SingleTickerProviderStateMixin {
  GpuTexture? _texture;
  
  WGPUTextureView? _wgpuTextureView; 
  WGPURenderPipeline? _renderPipeline;
  
  late Ticker _ticker;
  bool _isReady = false;
  
  final int _width = 500;
  final int _height = 500;

  WebGpuBindings get _wgpu => widget.engine.bindings;
  WGPUDevice get _device => widget.engine.device;
  WGPUQueue get _queue => widget.engine.queue;

  @override
  void initState() {
    super.initState();
    _initRawWebGpu();
  }

  Future<void> _initRawWebGpu() async {
    _texture = widget.engine.createTexture(_width, _height);
    
    _wgpuTextureView = _wgpu.wgpuTextureCreateView(_texture!.texture, nullptr);

    using((arena) {
      final shaderModule = _createShaderModule(arena, kShaderWgsl);

      final textureFormat = WGPUTextureFormat.WGPUTextureFormat_RGBA8Unorm;

      final colorTarget = arena<WGPUColorTargetState>();
      colorTarget.ref.format = textureFormat;
      colorTarget.ref.writeMask = WGPUColorWriteMask_All;

      final fragmentState = arena<WGPUFragmentState>();
      fragmentState.ref.module = shaderModule;
      fragmentState.ref.entryPoint = _createStringView(arena, "fs_main");
      fragmentState.ref.targetCount = 1;
      fragmentState.ref.targets = colorTarget;

      final vertexState = arena<WGPUVertexState>();
      vertexState.ref.module = shaderModule;
      vertexState.ref.entryPoint = _createStringView(arena, "vs_main");
      vertexState.ref.bufferCount = 0;

      final pipelineDesc = arena<WGPURenderPipelineDescriptor>();
      pipelineDesc.ref.vertex = vertexState.ref;
      pipelineDesc.ref.fragment = fragmentState;
      pipelineDesc.ref.primitive.topology = WGPUPrimitiveTopology.WGPUPrimitiveTopology_TriangleList;
      pipelineDesc.ref.multisample.count = 1;
      pipelineDesc.ref.multisample.mask = 0xFFFFFFFF;

      _renderPipeline = _wgpu.wgpuDeviceCreateRenderPipeline(_device, pipelineDesc);
      
      _wgpu.wgpuShaderModuleRelease(shaderModule);
    });

    setState(() => _isReady = true);
    _ticker = createTicker((elapsed) => _renderFrame(elapsed))..start();
  }

  void _renderFrame(Duration elapsed) {
    if (!_isReady || _texture == null) return;

    _texture!.beginAccess();

    using((arena) {
      const double cycleMs = 4000.0;
      double t = (elapsed.inMilliseconds % cycleMs) / (cycleMs / 2.0);
      double intensity = t > 1.0 ? 2.0 - t : t;

      final encoder = _wgpu.wgpuDeviceCreateCommandEncoder(_device, nullptr);

      final colorAttachment = arena<WGPURenderPassColorAttachment>();
      colorAttachment.ref.view = _wgpuTextureView!;
      colorAttachment.ref.loadOp = WGPULoadOp.WGPULoadOp_Clear;
      colorAttachment.ref.storeOp = WGPUStoreOp.WGPUStoreOp_Store;
      
      colorAttachment.ref.depthSlice = 0xFFFFFFFF; 

      colorAttachment.ref.clearValue.r = intensity * 0.2;
      colorAttachment.ref.clearValue.g = 0.0;
      colorAttachment.ref.clearValue.b = 0.3;
      colorAttachment.ref.clearValue.a = 1.0;

      final passDesc = arena<WGPURenderPassDescriptor>();
      passDesc.ref.colorAttachmentCount = 1;
      passDesc.ref.colorAttachments = colorAttachment;

      final renderPass = _wgpu.wgpuCommandEncoderBeginRenderPass(encoder, passDesc);
      _wgpu.wgpuRenderPassEncoderSetPipeline(renderPass, _renderPipeline!);
      _wgpu.wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
      _wgpu.wgpuRenderPassEncoderEnd(renderPass);

      final commandBuffer = _wgpu.wgpuCommandEncoderFinish(encoder, nullptr);
      final cmdListPtr = arena<WGPUCommandBuffer>();
      cmdListPtr.value = commandBuffer;
      _wgpu.wgpuQueueSubmit(_queue, 1, cmdListPtr);

      _wgpu.wgpuCommandBufferRelease(commandBuffer);
      _wgpu.wgpuCommandEncoderRelease(encoder);
    });

    _texture!.present();
  }

  WGPUShaderModule _createShaderModule(Arena arena, String code) {
    final wgsl = arena<WGPUShaderSourceWGSL>();
    wgsl.ref.chain.sType = WGPUSType.WGPUSType_ShaderSourceWGSL;
    wgsl.ref.code = _createStringView(arena, code);
    
    final desc = arena<WGPUShaderModuleDescriptor>();
    desc.ref.nextInChain = wgsl.cast();
    return _wgpu.wgpuDeviceCreateShaderModule(_device, desc);
  }

  WGPUStringView _createStringView(Arena arena, String s) {
    final native = s.toNativeUtf8(allocator: arena);
    final view = arena<WGPUStringView>();
    view.ref.data = native.cast();
    view.ref.length = s.length;
    return view.ref;
  }

  @override
  void dispose() {
    _ticker.dispose();
    if (_renderPipeline != null) _wgpu.wgpuRenderPipelineRelease(_renderPipeline!);
    if (_wgpuTextureView != null) _wgpu.wgpuTextureViewRelease(_wgpuTextureView!);
    _texture?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!_isReady) return const Scaffold(body: Center(child: CircularProgressIndicator()));
    
    return Scaffold(
      appBar: AppBar(title: const Text("Triangle - New Engine")),
      backgroundColor: Colors.black,
      body: Center(
        child: Container(
          width: _width.toDouble(),
          height: _height.toDouble(),
          decoration: BoxDecoration(
            border: Border.all(color: Colors.white24, width: 2),
          ),
          child: Texture(textureId: _texture!.textureId),
        ),
      ),
    );
  }
}

```

# History

This project started as a fork of https://github.com/google/flutter-sw-rend which can render bitmaps via method channels. First I added FFI to that project to reduce latency when uploading to the GPU. Then I wanted to add some shaders, so I created dx11 and OpenGL ES backends, using a shader translation layer from hlsl to glsl. Eventually that became difficult to manage so I switched to WebGPU, resulting in https://github.com/jacksonrl/flutter_webgpu_rend. After that, I decided to factor out the code relating to flutter native textures so others can perhaps make use of it (hence the name). Becuase of this, the code quality is varried, as sections got rewritten multiple times. It would probably look different had I started from scratch. Additionally, the project is currently only geared toward dawn WebGPU, and only tested for my specific use case, so some features may be missing or incomplete. 
