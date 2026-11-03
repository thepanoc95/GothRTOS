package com.example.gothrtosloader.runtime

import android.content.Context
import android.os.Build
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit

/**
 * Maps an Android ABI name to the directory names used under `prebuilt/`.
 */
internal fun normalizeAbi(abi: String?): String =
  when {
    abi.isNullOrBlank() -> ""
    abi.startsWith("arm64") -> "arm64-v8a"
    abi.startsWith("armeabi") || abi == "arm" -> "armeabi-v7a"
    abi.startsWith("x86_64") -> "x86_64"
    abi.startsWith("x86") -> "x86"
    else -> abi
  }

/**
 * Locates a native artifact in the extracted asset tree.
 *
 * Different builds have historically staged the binaries in different
 * layouts, so accept all of them rather than failing the install:
 *
 *   bin/<abi>/gothdroid
 *   <abi>/bin/gothdroid
 *   native/<abi>/bin/gothdroid
 *   native/bin/<abi>/gothdroid
 *   gothdroid/<abi>/bin/gothdroid
 */
internal fun findNativeArtifact(root: File, abi: String, name: String): File? {
  if (abi.isEmpty()) {
    return null
  }
  val layouts = listOf(
    "bin/$abi/$name",
    "$abi/bin/$name",
    "native/$abi/bin/$name",
    "native/bin/$abi/$name",
    "native/$abi/$name",
    "gothdroid/$abi/bin/$name",
  )
  for (layout in layouts) {
    val candidate = File(root, layout)
    if (candidate.isFile) {
      return candidate
    }
  }
  return root.walkTopDown().firstOrNull {
    it.isFile && it.name == name && it.path.contains("/$abi/")
  }
}

/**
 * Result of one root shell invocation.
 */
data class RootResult(
  val exitCode: Int,
  val output: String,
  val timedOut: Boolean = false,
) {
  val ok: Boolean
    get() = exitCode == 0 && !timedOut
}

/**
 * The runtime and root operations used by the launcher.
 *
 * Keeping this behind an interface makes the ViewModel testable without a
 * device, a rooted boot image or an Android framework instance.
 */
interface GothRuntimeApi {
  fun isRootAvailable(): Boolean

  fun isRuntimeInstalled(): Boolean

  fun installRuntime(): RootResult

  fun startRuntime(): RootResult

  fun stopRuntime(): RootResult

  fun runtimePid(): Int?

  fun readLog(maxLines: Int = 200): List<String>

  fun readStopRequested(): Boolean
}

/**
 * Owns the interaction with `su`, `/system/sbin/stop` and `/system/sbin/start`.
 *
 * The app never runs the GothRTOS UI itself. It installs a detached supervisor
 * script and asks root to run it:
 *
 *   su -c "sh /data/local/tmp/gothrtos/gothdroid-run.sh"
 *
 * The script runs /system/sbin/stop, starts the native gothdroid runtime and
 * finally runs /system/sbin/start from a trap. Because the script is detached
 * with setsid/nohup it survives the app process being killed by `stop`, which
 * is essential: Android's stop command tears down zygote and every app process
 * derived from it.
 */
class GothRuntime(private val context: Context) : GothRuntimeApi {

  private val remoteRoot: String
    get() = "/data/local/tmp/gothrtos"

  private val localRoot: File
    get() = File(context.filesDir, "gothrtos")

  /**
   * Executes one command as root. The command is interpreted by the device
   * shell, so callers must quote user-controlled paths.
   */
  fun shell(command: String, timeoutSeconds: Long = 60): RootResult {
    val result = runSu(arrayOf("su", "-c", command), command, timeoutSeconds)
    if (result.ok || !looksLikeUnsupportedSu(result.output)) {
      return result
    }
    // A few su implementations only understand the `su 0 sh -c` form.
    return runSu(arrayOf("su", "0", "sh", "-c", command), command, timeoutSeconds)
  }

  private fun runSu(
    arguments: Array<String>,
    command: String,
    timeoutSeconds: Long,
  ): RootResult {
    var outputFile: File? = null
    return try {
      outputFile = File.createTempFile("goth-shell-", ".log", context.cacheDir)
      val process = ProcessBuilder(*arguments)
        .redirectErrorStream(true)
        .redirectOutput(outputFile)
        .start()
      val finished = process.waitFor(timeoutSeconds, TimeUnit.SECONDS)
      if (!finished) {
        process.destroyForcibly()
        RootResult(-1, "command timed out: $command", timedOut = true)
      } else {
        RootResult(process.exitValue(), outputFile.readText())
      }
    } catch (error: Exception) {
      RootResult(-1, error.message ?: error.javaClass.simpleName)
    } finally {
      outputFile?.delete()
    }
  }

  private fun looksLikeUnsupportedSu(output: String): Boolean {
    val lower = output.lowercase()
    return lower.contains("invalid option") ||
      lower.contains("unknown option") ||
      lower.contains("usage: su")
  }

  override fun isRootAvailable(): Boolean {
    val result = shell("id -u", timeoutSeconds = 10)
    if (!result.ok) {
      return false
    }
    val lastLine = result.output.lineSequence().map { it.trim() }.lastOrNull { it.isNotEmpty() }
    return lastLine == "0" || result.output.contains("uid=0")
  }

  override fun isRuntimeInstalled(): Boolean {
    val root = quote(remoteRoot)
    val result = shell(
      "test -f $root/gothdroid && " +
        "test -f $root/SystemUI/SystemUI.lua && " +
        "test -f $root/gothdroid-run.sh",
      timeoutSeconds = 10,
    )
    return result.ok
  }

  override fun installRuntime(): RootResult {
    val staged = extractAssets()
      ?: return RootResult(-1, "could not extract the GothRTOS assets from the APK")

    val abis = Build.SUPPORTED_ABIS
      .map { normalizeAbi(it) }
      .filter { it.isNotEmpty() }
      .distinct()
    if (abis.isEmpty()) {
      return RootResult(-1, "could not determine the device ABI")
    }

    var chosenAbi = ""
    var binary: File? = null
    for (candidateAbi in abis) {
      binary = extractNativeArtifact(staged, candidateAbi, "gothdroid")
      if (binary != null) {
        chosenAbi = candidateAbi
        break
      }
    }
    if (binary == null) {
      return RootResult(
        -1,
        "no gothdroid binary for ${abis.joinToString("/")} in the APK assets; " +
          "build it with os/Android/build-native.sh and rebuild the app.\n" +
          stagedListing(staged),
      )
    }
    val library = extractNativeArtifact(staged, chosenAbi, "libgothgfx.so")

    val root = quote(remoteRoot)
    val source = quote(staged.absolutePath)
    val binaryPath = quote(binary.absolutePath)
    val libraryPath = library?.let { quote(it.absolutePath) }
    val command = buildString {
      appendLine("rm -rf $root")
      appendLine("mkdir -p $root || exit 1")
      appendLine("cp -R $source/. $root/ || exit 1")
      // The binary may live inside a directory that also has to make room for
      // the installed executable, so copy it out before clearing the tree.
      appendLine("cp $binaryPath $root/.gothdroid.new || exit 1")
      appendLine("rm -rf $root/gothdroid")
      appendLine("mv -f $root/.gothdroid.new $root/gothdroid || exit 1")
      if (libraryPath != null) {
        appendLine("mkdir -p $root/lib || exit 1")
        appendLine("cp $libraryPath $root/lib/libgothgfx.so || exit 1")
      }
      appendLine("rm -rf $root/bin $root/native")
      appendLine("chmod 755 $root/gothdroid-run.sh 2>/dev/null || true")
      appendLine("chmod 755 $root/gothdroid 2>/dev/null || true")
      appendLine("if [ ! -x $root/gothdroid ]; then")
      appendLine("  echo \"gothdroid was not installed as an executable\"")
      appendLine("  ls -la $root")
      appendLine("  exit 1")
      append("fi")
    }
    return shell(command, timeoutSeconds = 120)
  }

  override fun startRuntime(): RootResult {
    if (!isRuntimeInstalled()) {
      return RootResult(-1, "the GothRTOS runtime is not installed yet")
    }
    val root = quote(remoteRoot)
    val command = buildString {
      append("mkdir -p $root && rm -f $root/stop && ")
      append("chmod 755 $root/gothdroid-run.sh 2>/dev/null || true; ")
      append("chmod 755 $root/gothdroid 2>/dev/null || true; ")
      append("sh $root/gothdroid-run.sh $root")
    }
    return shell(command, timeoutSeconds = 30)
  }

  override fun stopRuntime(): RootResult {
    val root = quote(remoteRoot)
    val result = shell("mkdir -p $root && printf stop > $root/stop", timeoutSeconds = 10)
    if (!result.ok) {
      return result
    }
    // Give the UI a moment to notice the request and unwind cleanly.
    Thread.sleep(300)
    val pid = runtimePid()
    if (pid != null && pid > 1) {
      shell("kill $pid 2>/dev/null || true", timeoutSeconds = 10)
    }
    return result
  }

  override fun runtimePid(): Int? {
    val root = quote(remoteRoot)
    val result = shell(
      "pid=\"\$(cat $root/gothdroid.pid 2>/dev/null)\"; " +
        "if [ -n \"\$pid\" ] && kill -0 \"\$pid\" 2>/dev/null; then echo \"\$pid\"; fi",
      timeoutSeconds = 10,
    )
    if (!result.ok) {
      return null
    }
    return result.output.lineSequence().mapNotNull { it.trim().toIntOrNull() }.lastOrNull()
  }

  override fun readLog(maxLines: Int): List<String> {
    val root = quote(remoteRoot)
    val result = shell(
      "tail -n ${maxLines.coerceIn(1, 1000)} $root/gothdroid.log 2>/dev/null",
      timeoutSeconds = 10,
    )
    if (!result.ok || result.output.isBlank()) {
      return emptyList()
    }
    return result.output.lineSequence().filter { it.isNotBlank() }.toList()
  }

  override fun readStopRequested(): Boolean {
    val root = quote(remoteRoot)
    return shell("test -e $root/stop", timeoutSeconds = 10).ok
  }

  /** Returns the supervisor script and the userland files from APK assets. */
  private fun extractAssets(): File? {
    val target = localRoot
    return try {
      if (target.exists()) {
        target.deleteRecursively()
      }
      if (!target.exists() && !target.mkdirs()) {
        return null
      }
      copyAssetDirectory(ASSET_ROOT, target)
      // Some builds stage the native payload in a separate top-level asset
      // directory, for example assets/gothdroid/<abi>/bin/gothdroid. Copy it
      // into a neutral subdirectory so it cannot collide with the installed
      // executable.
      if (context.assets.list(NATIVE_ASSET_ROOT).orEmpty().isNotEmpty()) {
        val nativeStage = File(target, "native")
        nativeStage.mkdirs()
        copyAssetDirectory(NATIVE_ASSET_ROOT, nativeStage)
      }
      if (!File(target, "SystemUI/SystemUI.lua").isFile ||
        !File(target, "gothdroid-run.sh").isFile
      ) {
        return null
      }
      target
    } catch (error: Exception) {
      null
    }
  }

  private fun stagedListing(root: File): String {
    val files = root.walkTopDown()
      .filter { it.isFile }
      .take(40)
      .map { it.relativeTo(root).path }
      .toList()
    return if (files.isEmpty()) {
      "the extracted asset tree is empty"
    } else {
      "staged files:\n" + files.joinToString("\n")
    }
  }

  /**
   * Finds a native artifact in the extracted tree, falling back to opening
   * known APK asset paths directly. The direct path matters because
   * AssetManager.list() is not reliable for every asset tree shape, and a
   * missing binary should not turn into "build it yourself" when it is
   * actually present in the APK.
   */
  private fun extractNativeArtifact(staged: File, abi: String, name: String): File? {
    findNativeArtifact(staged, abi, name)?.let { return it }

    val layouts = listOf(
      "bin/$abi/$name",
      "$abi/bin/$name",
      "native/$abi/bin/$name",
      "$abi/$name",
    )
    for (root in listOf("gothrtos", "gothdroid")) {
      for (layout in layouts) {
        val destination = File(staged, "native/$abi/$name")
        if (copyAssetIfPresent("$root/$layout", destination)) {
          return destination
        }
      }
    }
    return null
  }

  private fun copyAssetIfPresent(assetPath: String, destination: File): Boolean {
    return try {
      context.assets.open(assetPath).use { input ->
        destination.parentFile?.mkdirs()
        destination.outputStream().use { output -> input.copyTo(output) }
      }
      true
    } catch (_: IOException) {
      false
    }
  }

  /**
   * AssetManager.list() returns an empty array for both files and empty
   * directories, so a failed open() means "directory" here.
   */
  private fun copyAssetDirectory(assetPath: String, destination: File) {
    val children = context.assets.list(assetPath).orEmpty()
    if (children.isEmpty()) {
      copyAssetFile(assetPath, File(destination, assetPath.substringAfterLast('/')))
      return
    }
    for (child in children) {
      val childAsset = "$assetPath/$child"
      val childChildren = context.assets.list(childAsset).orEmpty()
      if (childChildren.isEmpty()) {
        copyAssetFile(childAsset, File(destination, child))
      } else {
        val directory = File(destination, child)
        directory.mkdirs()
        copyAssetDirectory(childAsset, directory)
      }
    }
  }

  private fun copyAssetFile(assetPath: String, destination: File) {
    try {
      context.assets.open(assetPath).use { input ->
        destination.parentFile?.mkdirs()
        destination.outputStream().use { output -> input.copyTo(output) }
      }
    } catch (_: IOException) {
      destination.mkdirs()
    }
  }

  private fun quote(value: String): String {
    return "'" + value.replace("'", "'\\''") + "'"
  }

  private companion object {
    const val ASSET_ROOT = "gothrtos"
    const val NATIVE_ASSET_ROOT = "gothdroid"
  }
}
