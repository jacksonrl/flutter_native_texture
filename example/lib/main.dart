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