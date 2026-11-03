import org.gradle.api.tasks.Sync

plugins {
  alias(libs.plugins.android.application)
  alias(libs.plugins.compose.compiler)
  alias(libs.plugins.kotlin.serialization)
}

// os/Android, which contains the Lua userland, the supervisor script and the
// optional prebuilt native runtime.
val gothRoot = rootProject.projectDir.parentFile

android {
  namespace = "com.example.gothrtosloader"
  compileSdk = 36
  defaultConfig {
    applicationId = "com.thepanoc95.gothrtos.gothdroid"
    minSdk = 29
    targetSdk = 36
    versionCode = 1
    versionName = "1.0"
  }

  buildTypes {
    release {
      isMinifyEnabled = false
      proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
    }
  }
  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
  buildFeatures {
    compose = true
    aidl = false
    buildConfig = false
    shaders = false
  }

  sourceSets {
    getByName("main") {
      // build.gradle.kts stages gothrtos/** here; the runtime installer copies
      // it from AssetManager into /data/local/tmp/gothrtos.
      assets.directories.add(
        layout.buildDirectory.dir("generated/gothAssets").get().asFile.absolutePath
      )
    }
  }

  packaging {
    resources {
      excludes += "/META-INF/{AL2.0,LGPL2.1}"
    }
  }
}

/**
 * Stages the Lua userland, the root supervisor and (when present) the native
 * gothdroid binaries produced by os/Android/build-native.sh into APK assets.
 *
 * The assets end up as:
 *
 *   gothrtos/SystemUI/SystemUI.lua
 *   gothrtos/SurfaceLinger.lua
 *   gothrtos/devices/harpia.lua
 *   gothrtos/gothdroid-run.sh
 *   gothrtos/bin/<abi>/gothdroid
 */
val stageGothAssets =
  tasks.register<Sync>("stageGothAssets") {
    description = "Stages the GothRTOS runtime into the app's APK assets"
    into(layout.buildDirectory.dir("generated/gothAssets/gothrtos"))
    from(gothRoot.resolve("SystemUI")) { into("SystemUI") }
    from(gothRoot.resolve("devices")) { into("devices") }
    from(gothRoot.resolve("SurfaceLinger.lua"))
    from(gothRoot.resolve("gothdroid-run.sh"))
    // fileTree tolerates a missing prebuilt directory, so a debug APK can be
    // built before the NDK is installed.
    from(
      fileTree(gothRoot.resolve("prebuilt")) {
        include("*/gothdroid")
        include("*/libgothgfx.so")
      }
    ) {
      into("bin")
    }
  }

tasks.named("preBuild").configure { dependsOn(stageGothAssets) }

kotlin {
  jvmToolchain(17)
}

dependencies {
  val composeBom = platform(libs.androidx.compose.bom)
  implementation(composeBom)
  androidTestImplementation(composeBom)

  // Core Android dependencies
  implementation(libs.androidx.core.ktx)
  implementation(libs.androidx.lifecycle.runtime.ktx)
  implementation(libs.androidx.activity.compose)

  // Arch Components
  implementation(libs.androidx.lifecycle.runtime.compose)
  implementation(libs.androidx.lifecycle.viewmodel.compose)

  // Compose
  implementation(libs.androidx.compose.ui)
  implementation(libs.androidx.compose.ui.tooling.preview)
  implementation(libs.androidx.compose.material3)
  // Tooling
  debugImplementation(libs.androidx.compose.ui.tooling)
  // Instrumented tests
  androidTestImplementation(libs.androidx.compose.ui.test.junit4)
  debugImplementation(libs.androidx.compose.ui.test.manifest)

  // Local tests: jUnit, coroutines, Android runner
  testImplementation(libs.junit)
  testImplementation(libs.kotlinx.coroutines.test)

  // Instrumented tests: jUnit rules and runners
  androidTestImplementation(libs.androidx.test.core)
  androidTestImplementation(libs.androidx.test.ext.junit)
  androidTestImplementation(libs.androidx.test.runner)
  androidTestImplementation(libs.androidx.test.espresso.core)

  // Navigation
  implementation(libs.androidx.navigation3.ui)
  implementation(libs.androidx.navigation3.runtime)
  implementation(libs.androidx.lifecycle.viewmodel.navigation3)
}
