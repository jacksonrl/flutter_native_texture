import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';
import 'package:ffi/ffi.dart';

class FlutterNativeTexture {
  static final FlutterNativeTexture instance = FlutterNativeTexture._();
  late final DynamicLibrary dylib;

  late final Pointer<Void> Function(int w, int h) _create;
  late final int Function(Pointer<Void>) _getId;
  late final void Function(Pointer<Void>, Pointer<Uint8>, int, int, int, int) _upload;
  late final void Function(Pointer<Void>, Pointer<Uint8>) _download;
  late final void Function(Pointer<Void>) _present;
  late final void Function(Pointer<Void>) _dispose;
  late final Pointer<Void> Function(Pointer<Void>) _getDxgiHandle;
  late final Pointer<Void> Function(Pointer<Void>) _getAndroidWindow;
  late final Pointer<Void> Function(Pointer<Void>) _getAHardwareBuffer;

  FlutterNativeTexture._() {
    dylib = Platform.isWindows 
        ? DynamicLibrary.open('flutter_native_texture_plugin.dll') 
        : DynamicLibrary.open('libflutter_native_texture_android.so');

    _create = dylib.lookupFunction<Pointer<Void> Function(Int32, Int32), Pointer<Void> Function(int, int)>('flutter_native_texture_create_texture');
    _getId = dylib.lookupFunction<Int64 Function(Pointer<Void>), int Function(Pointer<Void>)>('flutter_native_texture_get_texture_id');
    _upload = dylib.lookupFunction<Void Function(Pointer<Void>, Pointer<Uint8>, Int32, Int32, Int32, Int32), void Function(Pointer<Void>, Pointer<Uint8>, int, int, int, int)>('flutter_native_texture_upload_texture');
    _download = dylib.lookupFunction<Void Function(Pointer<Void>, Pointer<Uint8>), void Function(Pointer<Void>, Pointer<Uint8>)>('flutter_native_texture_download_texture');
    _present = dylib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('flutter_native_texture_present_texture');
    _dispose = dylib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('flutter_native_texture_dispose_texture');
    _getDxgiHandle = dylib.lookupFunction<Pointer<Void> Function(Pointer<Void>), Pointer<Void> Function(Pointer<Void>)>('flutter_native_texture_get_dxgi_shared_handle');
    _getAndroidWindow = dylib.lookupFunction<Pointer<Void> Function(Pointer<Void>), Pointer<Void> Function(Pointer<Void>)>('flutter_native_texture_get_android_native_window');
    _getAHardwareBuffer = dylib.lookupFunction<Pointer<Void> Function(Pointer<Void>), Pointer<Void> Function(Pointer<Void>)>('flutter_native_texture_get_hardware_buffer');
  }

  Pointer<Void> createTexture(int w, int h) => _create(w, h);
  int getTextureId(Pointer<Void> handle) => _getId(handle);
  
  void uploadRect(Pointer<Void> handle, Uint8List data, int x, int y, int w, int h) {
    using((arena) {
      final ptr = arena<Uint8>(data.length);
      ptr.asTypedList(data.length).setAll(0, data);
      _upload(handle, ptr, x, y, w, h);
    });
  }

  void present(Pointer<Void> handle) => _present(handle);

  Uint8List download(Pointer<Void> handle, int w, int h) {
    final size = w * h * 4;
    final ptr = malloc<Uint8>(size);
    _download(handle, ptr);
    final list = Uint8List.fromList(ptr.asTypedList(size));
    malloc.free(ptr);
    return list;
  }

    /// get the DXGI handle (Windows only)
    Pointer<Void> getDxgiSharedHandle(Pointer<Void> ref) {
      if (!Platform.isWindows) return nullptr;
      return _getDxgiHandle(ref);
    }

    /// get the Native Window (Android only)
    Pointer<Void> getAndroidNativeWindow(Pointer<Void> ref) {
      if (!Platform.isAndroid) return nullptr;
      return _getAndroidWindow(ref);
    }

    /// get the Native Window (Android only)
    Pointer<Void> getAHardwareBuffer(Pointer<Void> ref) {
      if (!Platform.isAndroid) return nullptr;
      return _getAHardwareBuffer(ref);
    }

  void disposeTexture(Pointer<Void> handle) => _dispose(handle);
}