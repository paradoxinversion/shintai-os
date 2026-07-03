plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
    id("io.gitlab.arturbosch.detekt")
}

android {
    namespace = "com.saboteur.shintaiglass"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.saboteur.shintaiglass"
        minSdk = 31          // BLUETOOTH_CONNECT runtime-permission model; covers RayNeo X3 Pro
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
    }
}

// Kotlin static analysis. Config lives in android/detekt.yml. Advisory by default
// (findings are reported, not fatal); pass -PstrictDetekt to FAIL on any finding not
// already in detekt-baseline.xml — the pre-commit hook uses this to block new issues.
detekt {
    config.setFrom(rootProject.file("detekt.yml"))
    buildUponDefaultConfig = true
    ignoreFailures = !project.hasProperty("strictDetekt")
    val baseline = file("detekt-baseline.xml")   // regenerate: android/build.sh detektBaseline
    if (baseline.exists()) this.baseline = baseline
}

dependencies {
    implementation(project(":core"))   // ShintaiGatt/BleClient/Units/Readings — the shared contract
    implementation(platform("androidx.compose:compose-bom:2024.10.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.core:core-ktx:1.13.1")
}
