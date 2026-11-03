pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
        google()
    }
}
dependencyResolutionManagement {
    repositories {
        mavenCentral()
        google()
    }
}
rootProject.name = "cc700"
include(":core", ":testing", ":sync")

// :app needs the Android SDK and Google's Maven repository. It is included only where an SDK is
// configured (local.properties sdk.dir or ANDROID_HOME), so the JVM modules build anywhere.
val sdkDir = file("local.properties").takeIf { it.exists() }
    ?.readLines()?.firstOrNull { it.startsWith("sdk.dir=") }?.substringAfter("=")
    ?: System.getenv("ANDROID_HOME") ?: System.getenv("ANDROID_SDK_ROOT")
if (sdkDir != null && file(sdkDir).isDirectory) include(":app")
