plugins {
    id("com.android.application")
    kotlin("android")
}

android {
    namespace = "fr.felucca.midihub"
    compileSdk = 35

    defaultConfig {
        applicationId = "fr.felucca.midihub"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"
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
    testImplementation("junit:junit:4.13.2")
}
