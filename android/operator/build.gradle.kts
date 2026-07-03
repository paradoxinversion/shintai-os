plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
    id("io.gitlab.arturbosch.detekt")
}

// The Operator (phone) app: the stable, full-fidelity field console and the
// standalone fallback when the glasses aren't present. Unlike :glass it scans &
// pairs, records the stream to CSV, renders the ENVIRONMENT channel, and keeps a
// rolling history — capabilities that lean on the phone's dependable radio and
// storage. Styled to docs/style.md (phosphor-on-void), distinct from Glass.
android {
    namespace = "com.saboteur.shintaioperator"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.saboteur.shintaioperator"
        minSdk = 31          // BLUETOOTH_CONNECT/SCAN runtime-permission model (API 31+)
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

detekt {
    config.setFrom(rootProject.file("detekt.yml"))
    buildUponDefaultConfig = true
    ignoreFailures = !project.hasProperty("strictDetekt")
    val baseline = file("detekt-baseline.xml")   // regenerate: android/build.sh :operator:detektBaseline
    if (baseline.exists()) this.baseline = baseline
}

dependencies {
    implementation(project(":core"))   // ShintaiGatt/BleClient/Units/Readings — the shared contract
    implementation(platform("androidx.compose:compose-bom:2024.10.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.core:core-ktx:1.13.1")
}
