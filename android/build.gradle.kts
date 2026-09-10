plugins {
    id("com.android.library") version "8.13.2" apply false
    id("com.android.application") version "8.13.2" apply false
}

tasks.register<Exec>("verifyAndroidInputValues") {
    dependsOn("verifyAndroidAccessibilityValues")
    workingDir = rootDir.parentFile
    commandLine("python3", "tools/generate-android-input-values.py", "--check")
}

tasks.register<Exec>("verifyAndroidAccessibilityValues") {
    workingDir = rootDir.parentFile
    commandLine("python3", "tools/generate-android-accessibility-values.py", "--check")
}
