plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    id("io.gitlab.arturbosch.detekt")
}

// Shared library: the ONE mirror of CONTRACT.md's GATT table (ShintaiGatt), the
// BLE transport (ShintaiBleClient), the readings model + fold, and unit
// conversion. No Compose, no app entry point — both :glass and :operator depend
// on this so the contract can never drift between the two consumer apps.
android {
    namespace = "com.saboteur.shintai.core"
    compileSdk = 35

    defaultConfig {
        minSdk = 31          // BLUETOOTH_CONNECT runtime-permission model; covers RayNeo X3 Pro
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")   // pure-JVM unit tests for the :core merge/fold logic
}

detekt {
    config.setFrom(rootProject.file("detekt.yml"))
    buildUponDefaultConfig = true
    ignoreFailures = !project.hasProperty("strictDetekt")
    val baseline = file("detekt-baseline.xml")   // regenerate: android/build.sh :core:detektBaseline
    if (baseline.exists()) this.baseline = baseline
}
