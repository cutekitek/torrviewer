import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

val nativeAbis = providers.gradleProperty("abis")
    .orElse("arm64-v8a,armeabi-v7a,x86_64").get().split(",")
val nativeOutput = layout.buildDirectory.dir("generated/jniLibs")
val releaseSigningFile = file(providers.gradleProperty("releaseSigningProperties")
    .orElse("${System.getProperty("user.home")}/.android/torrview/release-signing.properties").get())
val releaseSigning = Properties().apply {
    if (releaseSigningFile.isFile) releaseSigningFile.inputStream().use { load(it) }
}
fun signingValue(name: String): String = requireNotNull(releaseSigning.getProperty(name)) {
    "Missing $name in release signing properties"
}

android {
    namespace = "org.torrview.app"
    compileSdk = 35
    ndkVersion = "28.2.13676358"
    defaultConfig {
        applicationId = "org.torrview.app"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    splits {
        abi {
            isEnable = true
            reset()
            include(*nativeAbis.toTypedArray())
            isUniversalApk = false
        }
    }
    buildFeatures { compose = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    sourceSets["main"].jniLibs.srcDir(nativeOutput)
    sourceSets["main"].assets.srcDir("../../assets")
    packaging { jniLibs.useLegacyPackaging = false }
    signingConfigs {
        if (releaseSigningFile.isFile) create("release") {
            storeFile = releaseSigningFile.parentFile.resolve(signingValue("storeFile"))
            storePassword = signingValue("storePassword")
            keyAlias = signingValue("keyAlias")
            keyPassword = signingValue("keyPassword")
        }
    }
    buildTypes {
        release {
            isDebuggable = false
            isMinifyEnabled = true
            isShrinkResources = true
            if (releaseSigningFile.isFile) signingConfig = signingConfigs.getByName("release")
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
}

val buildNative by tasks.registering(Exec::class) {
    group = "build"
    description = "Cross-compile the shared Torrview engine with Meson and the Android NDK"
    workingDir = rootProject.projectDir.parentFile
    val sdk = android.sdkDirectory
    val ndk = sdk.resolve("ndk/${android.ndkVersion}")
    val dependencies = providers.gradleProperty("nativeDeps")
        .orElse(rootProject.layout.buildDirectory.dir("dependencies").get().asFile.absolutePath)
    val host = providers.gradleProperty("nativeHost").orElse("local").get()
    val arguments = listOf(
        providers.gradleProperty("python").orElse(if (host == "wsl") "python3" else "python").get(),
        "scripts/build_android.py", "--ndk", providers.gradleProperty("nativeNdk").orElse(ndk.absolutePath).get(),
        "--deps", dependencies.get(), "--abis", nativeAbis.joinToString(","),
        "--output", "android/app/build/generated/jniLibs",
    )
    if (host == "wsl") {
        commandLine(listOf("wsl", "--distribution", providers.gradleProperty("wslDistribution").get(),
            "--exec", "env", "PATH=" + providers.gradleProperty("nativePath").orElse("/usr/local/bin:/usr/bin:/bin").get()) + arguments)
    } else {
        require(host == "local") { "nativeHost must be local or wsl" }
        commandLine(arguments)
    }
    // Always invoke Ninja: it owns native dependency tracking, including headers and archives.
    outputs.upToDateWhen { false }
}
tasks.named("preBuild") { dependsOn(buildNative) }

dependencies {
    implementation(platform("androidx.compose:compose-bom:2025.04.01"))
    androidTestImplementation(platform("androidx.compose:compose-bom:2025.04.01"))
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.tv:tv-material:1.0.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.1")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
