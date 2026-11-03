// :sync — the app's logic without Android: unit session and pairing, the workout and route library,
// ride sync and the upload queue. Pure Kotlin/JVM so the whole WP6 flow runs against simunit here; :app
// adds only Compose screens and Android implementations of the store and connector interfaces.
plugins { kotlin("jvm") }
kotlin { compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17); allWarningsAsErrors.set(true) } }
java { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
dependencies {
    api(project(":core"))
    api(project(":testing"))
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    api("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.3")
    testImplementation("org.junit.jupiter:junit-jupiter:5.11.3")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher:1.11.3")
    testImplementation("com.squareup.okhttp3:mockwebserver:4.12.0")
}
tasks.test {
    useJUnitPlatform()
    systemProperty("cc700.root", rootProject.projectDir.parentFile.absolutePath)
    testLogging { events("failed"); exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL }
}
