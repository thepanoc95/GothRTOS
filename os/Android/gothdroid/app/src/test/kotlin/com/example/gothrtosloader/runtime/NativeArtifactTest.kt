package com.example.gothrtosloader.runtime

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class NativeArtifactTest {

  @Test
  fun normalizeAbi_mapsAndroidNames() {
    assertEquals("arm64-v8a", normalizeAbi("arm64-v8a"))
    assertEquals("arm64-v8a", normalizeAbi("arm64"))
    assertEquals("armeabi-v7a", normalizeAbi("armeabi-v7a"))
    assertEquals("armeabi-v7a", normalizeAbi("armeabi"))
    assertEquals("armeabi-v7a", normalizeAbi("arm"))
    assertEquals("x86_64", normalizeAbi("x86_64"))
    assertEquals("x86", normalizeAbi("x86"))
    assertEquals("", normalizeAbi(null))
    assertEquals("", normalizeAbi(""))
  }

  @Test
  fun findNativeArtifact_acceptsCanonicalLayout() {
    withTempRoot { root ->
      val binary = file(root, "bin/armeabi-v7a/gothdroid")
      assertNotNull(binary)
      assertEquals(binary, findNativeArtifact(root, "armeabi-v7a", "gothdroid"))
    }
  }

  @Test
  fun findNativeArtifact_acceptsAbiFirstLayout() {
    withTempRoot { root ->
      val binary = file(root, "armeabi-v7a/bin/gothdroid")
      assertNotNull(binary)
      assertEquals(binary, findNativeArtifact(root, "armeabi-v7a", "gothdroid"))
    }
  }

  @Test
  fun findNativeArtifact_acceptsLegacyGothdroidRoot() {
    withTempRoot { root ->
      val binary = file(root, "gothdroid/armeabi-v7a/bin/gothdroid")
      assertNotNull(binary)
      assertEquals(binary, findNativeArtifact(root, "armeabi-v7a", "gothdroid"))
    }
  }

  @Test
  fun findNativeArtifact_acceptsNativeStagingLayout() {
    withTempRoot { root ->
      val binary = file(root, "native/armeabi-v7a/bin/gothdroid")
      assertNotNull(binary)
      assertEquals(binary, findNativeArtifact(root, "armeabi-v7a", "gothdroid"))
    }
  }

  @Test
  fun findNativeArtifact_returnsNullWhenMissing() {
    withTempRoot { root ->
      assertNull(findNativeArtifact(root, "armeabi-v7a", "gothdroid"))
      assertNull(findNativeArtifact(root, "", "gothdroid"))
    }
  }

  private fun withTempRoot(block: (File) -> Unit) {
    val root = Files.createTempDirectory("goth-assets").toFile()
    try {
      block(root)
    } finally {
      root.deleteRecursively()
    }
  }

  private fun file(root: File, path: String): File {
    val file = File(root, path)
    file.parentFile?.mkdirs()
    file.writeText("binary")
    return file
  }
}
