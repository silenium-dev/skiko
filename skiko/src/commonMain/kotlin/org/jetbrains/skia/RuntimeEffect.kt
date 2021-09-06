@file:Suppress("NESTED_EXTERNAL_DECLARATION")
package org.jetbrains.skia

import org.jetbrains.skia.impl.Library.Companion.staticLoad
import org.jetbrains.skia.impl.RefCnt
import org.jetbrains.skia.impl.Native
import org.jetbrains.skia.impl.Stats
import org.jetbrains.skia.ExternalSymbolName
import kotlin.jvm.JvmStatic

class RuntimeEffect internal constructor(ptr: Long) : RefCnt(ptr) {
    companion object {
        fun makeForShader(sksl: String?): RuntimeEffect {
            Stats.onNativeCall()
            return RuntimeEffect(_nMakeForShader(sksl))
        }

        fun makeForColorFilter(sksl: String?): RuntimeEffect {
            Stats.onNativeCall()
            return RuntimeEffect(_nMakeForColorFilter(sksl))
        }

        @JvmStatic
        @ExternalSymbolName("org_jetbrains_skia_RuntimeEffect__1nMakeShader")
        external fun _nMakeShader(
            runtimeEffectPtr: Long, uniformPtr: Long, childrenPtrs: LongArray?,
            localMatrix: FloatArray?, isOpaque: Boolean
        ): Long

        @JvmStatic
        @ExternalSymbolName("org_jetbrains_skia_RuntimeEffect__1nMakeForShader")
        external fun _nMakeForShader(sksl: String?): Long
        @JvmStatic
        @ExternalSymbolName("org_jetbrains_skia_RuntimeEffect__1nMakeForColorFilter")
        external fun _nMakeForColorFilter(sksl: String?): Long

        init {
            staticLoad()
        }
    }

    fun makeShader(
        uniforms: Data?, children: Array<Shader?>?, localMatrix: Matrix33?,
        isOpaque: Boolean
    ): Shader {
        Stats.onNativeCall()
        val childCount = children?.size ?: 0
        val childrenPtrs = LongArray(childCount)
        for (i in 0 until childCount) childrenPtrs[i] = Native.Companion.getPtr(children!![i])
        val matrix = localMatrix?.mat
        return Shader(_nMakeShader(_ptr, getPtr(uniforms), childrenPtrs, matrix, isOpaque))
    }
}