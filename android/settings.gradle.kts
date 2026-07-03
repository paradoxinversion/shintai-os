pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "ShintaiOS"
include(":core")       // shared BLE contract + transport (no UI)
include(":glass")      // RayNeo X3 Pro HUD app  (com.saboteur.shintaiglass)
include(":operator")   // phone field-console app (com.saboteur.shintaioperator)
