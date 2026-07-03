#!/bin/zsh
# Build/lint the Shintai Glass Android app from the command line.
#
# The app needs JDK 21, but the shell's default `java` on this machine is 1.8, so a
# bare `./gradlew` fails. This wrapper points JAVA_HOME at the JDK bundled with
# Android Studio (its JBR) and forwards its args to Gradle.
#
#   android/build.sh                 # assembleDebug + lint (default)
#   android/build.sh assembleDebug   # just the debug APK
#   android/build.sh lint            # just Android lint
#   android/build.sh detekt          # Kotlin static analysis (see app/build.gradle.kts)
#   android/build.sh test            # unit tests
#
# Override the JDK with SHINTAI_JAVA_HOME if yours lives elsewhere.
set -euo pipefail

DIR="${0:A:h}"
JBR="${SHINTAI_JAVA_HOME:-/Applications/Android Studio.app/Contents/jbr/Contents/Home}"

if [[ ! -x "$JBR/bin/java" ]]; then
  print -u2 "No JDK at: $JBR"
  print -u2 "Set SHINTAI_JAVA_HOME to a JDK 17+ (Android Studio's bundled JBR is ideal)."
  exit 1
fi
export JAVA_HOME="$JBR"

# Default task set when none given: build the app and run lint.
if [[ $# -eq 0 ]]; then
  set -- assembleDebug lint
fi

cd "$DIR"
exec ./gradlew "$@"
