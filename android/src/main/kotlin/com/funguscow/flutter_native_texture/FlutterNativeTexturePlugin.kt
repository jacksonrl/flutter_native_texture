package com.funguscow.flutter_native_texture

import android.view.Surface
import androidx.annotation.Keep
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.view.TextureRegistry
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

class FlutterNativeTexturePlugin : FlutterPlugin {
  @Keep
  companion object {
    private var textureRegistry: TextureRegistry? = null
    private val producers = ConcurrentHashMap<Int, TextureRegistry.SurfaceProducer>()
    private val nextHandle = AtomicInteger(1)

    @JvmStatic
    @Keep
    fun createTexture(width: Int, height: Int): Int {
      val reg = textureRegistry ?: return -1
      val producer = reg.createSurfaceProducer()
      producer.setSize(width, height)
      val handle = nextHandle.getAndIncrement()
      producers[handle] = producer
      return handle
    }

    @JvmStatic
    @Keep
    fun getTextureId(handle: Int): Long {
      return producers[handle]?.id() ?: -1L
    }

    @JvmStatic
    @Keep
    fun getSurface(handle: Int): Surface? {
      return producers[handle]?.getSurface()
    }

    @JvmStatic
    @Keep
    fun disposeTexture(handle: Int) {
      producers.remove(handle)?.release()
    }
  }

  override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
    System.loadLibrary("flutter_native_texture_android")
    textureRegistry = binding.textureRegistry
  }

  override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
    textureRegistry = null
    producers.values.forEach { it.release() }
    producers.clear()
  }
}
