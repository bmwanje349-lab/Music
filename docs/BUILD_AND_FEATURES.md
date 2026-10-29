# Luma Music

Luma Music is an Android-first local music player built around the repository's existing C++ audio engines. This app layer adds new files only; it does not replace or edit the pre-existing C++ engine source files.

## Design priorities

- Make local music easy to find, search, play, revisit, and favourite.
- Provide an inviting dark interface with lime highlights and an art-forward Now Playing screen.
- Keep playback running outside the activity, with Android media-session, notification, and lock-screen controls.
- Keep sound controls understandable for casual listeners while still offering a full 16-band PEQ.
- Keep local music scanning on-device and ask for Android's audio-library permission before reading indexed audio.

## Playback and DSP architecture

- Media3 ExoPlayer owns the playback queue, audio focus, noisy-output handling, and playback lifecycle.
- MediaSessionService publishes the session for Android system controls.
- DspAudioProcessor is inserted into the Media3 audio sink for stereo PCM16 output.
- JNI routes the signal through stereo_dsp::EngineSelector, then through the existing 16-band peq::Engine.
- The selector's Off, Consumer Enhancer, and Halo Bloom options choose one enhancer at a time. The PEQ is a separate stage and can be switched independently.
- The UI saves sound controls and favourite IDs locally using SharedPreferences.

## Project layout

- app/src/main/java/com/bmwanje/luma/data/ — local track model, MediaStore scan, favourites
- app/src/main/java/com/bmwanje/luma/playback/ — Media3 service, native DSP processor, presets and settings
- app/src/main/java/com/bmwanje/luma/ui/ — home, library, Now Playing, equalizer and settings screens
- app/src/main/cpp/ — JNI bridge and CMake definition. Existing root-level engines are compiled from their current paths.

## Build

Open this repository in a recent Android Studio version with Android SDK 36, NDK 27.2.12479018, CMake 3.22.1, and JDK 17 installed. Allow Gradle to sync, then run the app assembleDebug task or this command:

    gradle --no-daemon assembleDebug testDebugUnitTest

The GitHub Actions workflow uses Gradle 8.13 and Java 17, compiles the native library for the configured ABIs, runs unit tests, and stores the debug APK as a workflow artifact on success.

## Android permissions

The app requests READ_MEDIA_AUDIO on Android 13 and later and READ_EXTERNAL_STORAGE on older Android versions. Playback service declarations include the media-playback foreground-service permissions used for background playback on current Android releases.

## Current limits

- This first app pass scans Android's MediaStore audio index; it does not yet add playlist editing, tag editing, network streaming, lyrics lookup, a home-screen widget, cloud sync, or Android Auto-specific screens.
- DSP currently targets stereo PCM16 audio in the custom processor. Other decoded formats should remain on Media3's normal processor path.
- The supplied C++ engines are integrated by linking existing source files. Their audio behavior, CPU cost, and audio-format compatibility still need device-level listening and measurement.
- Android build success, device playback stability, permissions, audio focus, and artifact packaging are verified by the CI workflow when it runs successfully, not inferred from source creation.
