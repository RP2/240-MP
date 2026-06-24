# Jellyfin Module

Streams media from a Jellyfin server. Mirrors the Plex module's architecture.

## Auth

Quick Connect only — enter your server URL, a 6-digit code appears, approve it at
`{server}/web/#/quickconnect`. No password auth (Quick Connect is TV/gamepad-friendly).

## Settings

| Setting | Description |
|---|---|
| Enabled | Toggle module on/off |
| Libraries | Choose which libraries to browse |
| Video Quality | HLS transcode resolution: **480p (NTSC CRT)**, **576p (PAL CRT)**, **720p**, **1080p** |
| Resume Playback | Ask / Always / Never |
| Autoplay Next Episode | Toggle on/off |
| Sign out | Logout |

## Browse

Items display as a Plex-style text list (28px rows, 24px font). The list view is intentionally simple — consistent with Plex, no thumbnail grid.

## Playback

Launches mpv subprocess. The `video_quality` setting controls the transcode resolution:
- **Transcode**: `master.m3u8` — HLS stream, server-side decode + re-encode

mpv receives `--ytdl=no` unconditionally to prevent yt-dlp from intercepting media URLs.

## Pi / CRT notes

- Pi 3B+ uses `--hwdec=v4l2m2m` (zero-copy) for smooth playback. This path cannot crop — widescreen content gets black bars on 4:3 CRTs.
- Pi 4/5 handles cropping natively.
- Use the CRT quality presets to offload to NAS transcoding on weak hardware.

## Files

```
modules/jellyfin/
  manifest.json              Module registration + settings
  views/Root.qml              Navigation router
  views/Auth.qml              Server URL entry + Quick Connect
  views/QuickConnect.qml      Code display + approval polling
  views/Libraries.qml         Library list
  views/Items.qml             Item browser (Plex-style list)
  views/Item.qml              Detail + PLAY
  views/ItemSeason.qml        TV show seasons → episodes
  views/Player.qml            Playback orchestrator (launches mpv)

src/modules/jellyfin/
  JellyfinBackend.h           Q_INVOKABLE API + signal declarations
  JellyfinBackend.cpp         HTTP client, auth, browse, playback, settings
```

## Registration

```cpp
// src/main.cpp
#include "modules/jellyfin/JellyfinBackend.h"
JellyfinBackend jellyfinBackend(appRoot, dataRoot);
appCore.registerModule("com.240mp.jellyfin", "jellyfinBackend", &jellyfinBackend, ctx);
```
