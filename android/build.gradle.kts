plugins {
    id("com.android.library") version "8.13.2" apply false
    id("com.android.application") version "8.13.2" apply false
}

tasks.register<Exec>("verifyAndroidInputValues") {
    workingDir = rootDir.parentFile
    commandLine("python3", "tools/generate-android-input-values.py", "--check")
}
