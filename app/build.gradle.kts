import org.gradle.nativeplatform.platform.internal.DefaultNativePlatform.getCurrentOperatingSystem
import java.util.UUID

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    kotlin("plugin.serialization")
}

android {
    namespace = "cassia.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "cassia.app"
        minSdk = 29
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        vectorDrawables {
            useSupportLibrary = true
        }
    }

    ndkVersion = "26.1.10909125"

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            ndk {
                //noinspection ChromeOsAbiSupport
                abiFilters += "arm64-v8a"
            }
        }
        debug {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            ndk {
                //noinspection ChromeOsAbiSupport
                abiFilters += "arm64-v8a"
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_19
        targetCompatibility = JavaVersion.VERSION_19
    }
    kotlinOptions {
        jvmTarget = "19"
    }
    buildFeatures {
        compose = true
    }
    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.4"
    }
    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildToolsVersion = "34.0.0"
}

project.tasks.register("buildCassiaExt") {
    doLast {
        val cassiaExtCfg = project.file("cassiaext.cfg").readText().trim().lines()
        val cassiaExtPath = cassiaExtCfg.getOrNull(0) ?: throw RuntimeException("cassiaext.cfg is empty")
        var cassiaExtHostPath = cassiaExtPath
        val cfgWrapper = cassiaExtCfg.getOrNull(1)

        var shellWrapper = arrayOf<String>()
        if (getCurrentOperatingSystem().isWindows) {
            // Look for the wsl.exe in PATH
            val process = ProcessBuilder().command("where", "wsl").start()
            if (process.waitFor() != 0)
                throw RuntimeException("'where wsl' failed with exit code ${process.exitValue()}")
            shellWrapper += process.inputReader().use { it.readLines().getOrNull(0) }?.trim()
                ?: throw RuntimeException("WSL not found in PATH")
            shellWrapper += "--"

            // Convert the path to WSL format
            val process2 = ProcessBuilder().command(*shellWrapper, "wslpath", "-wa", cassiaExtPath).start()
            if (process2.waitFor() != 0)
                throw RuntimeException("WSL failed to convert path: ${process2.errorStream.reader().readText()}")
            cassiaExtHostPath = process2.inputStream.use { it.reader().readText() }.trim()
        } else if (getCurrentOperatingSystem().isLinux) {
            shellWrapper = arrayOf("/bin/bash", "-c")
        } else {
            throw RuntimeException("Unsupported operating system")
        }

        if (cfgWrapper != null)
            shellWrapper += cfgWrapper.split(" ").toTypedArray()

        if (!project.file(cassiaExtHostPath).exists())
            throw RuntimeException("CassiaExt path does not exist: $cassiaExtHostPath")

        val process3 = ProcessBuilder().command(*shellWrapper, "ninja -C $cassiaExtPath all").start()
        if (process3.waitFor() != 0)
            throw RuntimeException("Ninja failed to build CassiaExt: ${process3.exitValue()}\nSTDOUT:\n${process3.inputStream.reader().readText().trim()}\nSTDERR:\n${process3.errorStream.reader().readText().trim()}")

        val prefixTarGz = project.file("$cassiaExtHostPath/prefix.tar.gz")
        if (!prefixTarGz.exists())
            throw RuntimeException("CassiaExt build did not produce tarball: $cassiaExtHostPath/prefix.tar.gz")
        temporaryDir.deleteRecursively() // Clean up any previous build
        prefixTarGz.copyTo(temporaryDir.resolve("cassiaext.tar.gz"), overwrite = true)

        val idFile = temporaryDir.resolve("cassiaext.id")
        idFile.writeText(UUID.randomUUID().toString())

        val assetsSet = project.android.sourceSets["main"].assets
        assetsSet.setSrcDirs(setOf(assetsSet.srcDirs + temporaryDir))
    }

    outputs.upToDateWhen { false } // Always run this task
}


project.tasks.whenTaskAdded(fun(t: Task) {
    if (t.name == "generateDebugAssets" || t.name == "generateReleaseAssets")
        t.dependsOn("buildCassiaExt")
})

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation(platform("androidx.compose:compose-bom:2024.02.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.core:core-splashscreen:1.0.1")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.0")
    implementation("org.apache.commons:commons-compress:1.21")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.5.1")
    androidTestImplementation(platform("androidx.compose:compose-bom:2024.02.00"))
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
