// The Android plugins come from Google's Maven repository. They are put on the build classpath only
// where an Android SDK is configured (the same test as settings.gradle.kts uses to include :app), so the
// JVM modules (:core, :testing, :sync) build and test without the SDK or Google's repository.
buildscript {
    val props = file("local.properties")
    val sdk = (if (props.exists()) props.readLines().firstOrNull { it.startsWith("sdk.dir=") }?.substringAfter("=") else null)
        ?: System.getenv("ANDROID_HOME") ?: System.getenv("ANDROID_SDK_ROOT")
    if (sdk != null && file(sdk).isDirectory) {
        repositories { google(); mavenCentral(); gradlePluginPortal() }
        dependencies {
            classpath("com.android.tools.build:gradle:8.7.3")
            classpath("org.jetbrains.kotlin:compose-compiler-gradle-plugin:2.0.21")
            classpath("com.google.devtools.ksp:symbol-processing-gradle-plugin:2.0.21-1.0.28")
        }
    }
}

plugins {
    kotlin("jvm") version "2.0.21" apply false
    kotlin("plugin.serialization") version "2.0.21" apply false
}
