# Luma song-transition design

Research date: 29 October 2026

## Patterns borrowed from established players

- **Spotify separates three ideas rather than treating them as one feature.** Its support guide describes Crossfade as an overlap between two tracks, Automix as beat-matched transitions for selected playlists, and Gapless playback as removing gaps or pauses. These are different playback behaviors, with different eligibility.
  - https://support.spotify.com/np-en/article/tracks-transitions/
  - https://newsroom.spotify.com/2025-08-19/mix-your-favorite-playlists-seamlessly-by-adding-your-own-transitions/
- **Apple Music makes the transition style selectable.** Crossfade uses a chosen duration; AutoMix can choose a transition based on the music and may remove silence or use a simpler blend. Apple also notes that albums and some genres may play without transitions.
  - https://support.apple.com/en-ug/105067
  - https://support.apple.com/en-au/guide/iphone/iphadf2fe1f4/ios
- **Local Android players expose practical mixing controls.** Poweramp lists gapless playback and crossfade as separate capabilities; AIMP exposes crossfade settings and documents that fading can conflict with gapless playback. AIMP's history also notes next-track preloading to reduce the delay when switching tracks.
  - https://powerampapp.com/
  - https://aimp.ru/?do=features&os=android
  - https://aimp.su/?do=changelog&f=174&merge=1&os=android&s=40

## What Luma implements

### Smart (default)

- Prepares the next item before the current track ends.
- Uses a real two-player, equal-power volume crossfade for tracks that do not look like part of the same album run.
- Preserves same-album/same-artist sequencing without an imposed overlap, to avoid blending intentional album transitions.
- Uses a short click-safe fade for manual Next/Previous and when the user selects another song.

### Crossfade

- Blends the outgoing and incoming tracks over the chosen duration for every eligible next track.
- The incoming track is prepared at low volume before the transition starts. Both paths use Luma's native DSP settings, and one shared audio-focus request prevents the two playback pipelines competing for focus.

### Fade

- Fades the outgoing track, then brings the next track up. It does not intentionally overlap two tracks.

### Off

- Disables automatic fade/crossfade behavior. Manual skips still use a short fade to avoid clicks.

The transition duration is user-selectable from 1 to 8 seconds. Luma uses an equal-power cosine/sine envelope during true crossfades to avoid the perceived level dip of a naive linear crossfade.

## Boundaries and honesty

This is **not a full DJ AutoMix engine**. Luma does not yet analyse BPM, musical key, beats, phrase boundaries or silence regions, and therefore does not claim beat-matched transitions. Smart mode currently uses local album/artist metadata as a conservative heuristic. Files with missing or inaccurate tags may be treated as separate songs and crossfaded.

The standby player is prepared shortly before the transition. If a file takes too long to prepare, normal playback takes priority over a late or broken blend. Device-level listening tests across codecs, Bluetooth outputs and Android audio-focus interruptions are still required.

## Validation

The Android CI workflow compiles the app and runs its unit tests. A successful CI build is not a substitute for listening tests on a physical Android device.
