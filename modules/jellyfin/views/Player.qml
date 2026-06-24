import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()
    // Emitted when autoplay advances in place, so Root can repoint the BACK
    // target to the now-playing episode's detail instead of the original one.
    signal updateBackItem(var item)

    property string streamUrl:      navParams.streamUrl      || ""
    property string itemId:         navParams.itemId         || ""
    property string seriesId:       navParams.seriesId       || ""
    property string mediaSourceId:  navParams.mediaSourceId  || itemId
    property string itemTitle:      navParams.title          || ""
    property int    viewOffset:     navParams.viewOffset     || 0
    property int    parentIndex:    navParams.parentIndex    || 0
    property int    index:          navParams.index          || 0
    property var    audioStreams:       navParams.audioStreams     || []
    property var    subtitleStreams:    navParams.subtitleStreams  || []
    property string selectedAudioId:    navParams.selectedAudioId    || ""
    property string selectedSubtitleId: navParams.selectedSubtitleId || ""

    property bool isTranscoding: streamUrl.indexOf("master.m3u8") >= 0

    // Autoplay next episode
    property bool   autoplayNext:       false
    property bool   pendingNextEpisode: false
    property string carryAudioLang:     ""
    property string carrySubLang:       "__off__"

    // When true, skip the "Resume / Start from beginning" dialog and always resume
    property bool resumeSkip: navParams.resumeSkip || false

    property int    audioIdx:    0
    property int    subtitleIdx: -1

    property bool stoppedReported: false
    property bool playbackStarted: false
    property bool overlayVisible:  false
    property int  choiceIndex:     0
    property string resumeSetting: "ask"

    property int lastKnownPositionMs: 0
    property int lastKnownDurationMs: 0

    focus: true

    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                choiceIndex = 0
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                choiceIndex = 1
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                overlayVisible = false
                if (choiceIndex === 0) {
                    beginPlayback(viewOffset)
                } else {
                    beginPlayback(0)
                }
                event.accepted = true
            }
        } else {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
                stopPlayback()
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Space) {
                mpvController.sendKey("SPACE")
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                mpvController.sendKey("UP")
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                mpvController.sendKey("DOWN")
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                mpvController.sendKey("LEFT")
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                mpvController.sendKey("RIGHT")
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                mpvController.sendKey("ENTER")
                event.accepted = true
            }
        }
    }

    function msToTicks(ms) {
        return ms * 10000
    }

    function initStreamIndices() {
        var selAudio = String(selectedAudioId || "")
        var selSub   = String(selectedSubtitleId || "")
        audioIdx = 0
        for (var i = 0; i < audioStreams.length; i++) {
            if (String(audioStreams[i].id || "") === selAudio) { audioIdx = i; break }
        }
        // Subtitle track: -1 means off; otherwise find the 0-based index in subtitleStreams.
        subtitleIdx = -1
        for (var j = 0; j < subtitleStreams.length; j++) {
            if (String(subtitleStreams[j].id || "") === selSub) { subtitleIdx = j; break }
        }
        captureCarryLanguages()
    }

    // Record the language of the current audio/subtitle selection so the next
    // episode (which has different per-file stream IDs) can be matched by language.
    function captureCarryLanguages() {
        var a = audioStreams[audioIdx]
        carryAudioLang = (a && a.language) ? a.language : ""
        var s = subtitleStreams[subtitleIdx]
        carrySubLang = (subtitleIdx === -1 || !s) ? "__off__" : (s.language || "")
    }

    // Select audioIdx/subtitleIdx on the current stream lists to match the carried
    // languages. Falls back to the first audio track / subtitles-off when no match.
    function applyCarryLanguages() {
        audioIdx = 0
        for (var i = 0; i < audioStreams.length; i++) {
            if (carryAudioLang && audioStreams[i].language === carryAudioLang) { audioIdx = i; break }
        }
        subtitleIdx = -1
        if (carrySubLang !== "__off__" && carrySubLang !== "") {
            for (var j = 0; j < subtitleStreams.length; j++) {
                if (subtitleStreams[j].language === carrySubLang) { subtitleIdx = j; break }
            }
        }
    }

    function reportStopped(finalPositionMs, finalDurationMs, failed) {
        if (stoppedReported) return
        stoppedReported = true
        var pos = lastKnownPositionMs || finalPositionMs
        jellyfinBackend.report_playback_stopped(itemId, mediaSourceId, msToTicks(pos), failed || false)
    }

    function stopPlayback() {
        reportStopped(mpvController.position, mpvController.duration)
        mpvController.stop()
    }

    // Swap the player's context to the next episode in place (no navigation) and
    // begin playing it from the beginning, carrying over the track languages.
    function advanceToEpisode(detail) {
        itemId         = detail.itemId         || ""
        mediaSourceId  = detail.mediaSourceId  || detail.itemId || ""
        itemTitle      = detail.title          || ""
        audioStreams   = detail.audioStreams   || []
        subtitleStreams= detail.subtitleStreams|| []
        seriesId       = detail.seriesId       || ""
        parentIndex    = detail.parentIndex    || 0
        index          = detail.index          || 0

        // Fresh-start state for the new episode
        viewOffset           = 0
        stoppedReported      = false
        playbackStarted      = false
        lastKnownPositionMs  = 0
        lastKnownDurationMs  = 0
        resumeSkip           = false

        // Repoint the BACK target so exiting returns to THIS episode's detail
        updateBackItem({
            itemId: detail.itemId,
            type: detail.type || "episode",
            title: detail.title || "",
            grandparentTitle: detail.grandparentTitle || "",
            parentIndex: detail.parentIndex,
            index: detail.index
        })

        // Match the carried languages onto this episode's stream lists
        applyCarryLanguages()
        selectedAudioId    = (audioStreams[audioIdx] && audioStreams[audioIdx].id) ? String(audioStreams[audioIdx].id) : ""
        selectedSubtitleId = (subtitleIdx >= 0 && subtitleStreams[subtitleIdx] && subtitleStreams[subtitleIdx].id) ? String(subtitleStreams[subtitleIdx].id) : ""
        captureCarryLanguages()

        // Start a new playback session on the server so progress tracking
        // and the Continue-Watching shelf work for the auto-advanced episode.
        jellyfinBackend.report_playback_start(detail.itemId, detail.mediaSourceId || detail.itemId,
                                               selectedAudioId, selectedSubtitleId)

        // Request the new stream URL
        pendingNextEpisode = true
        var audioStreamIdx = selectedAudioId ? parseInt(selectedAudioId) : -1
        var subStreamIdx   = selectedSubtitleId ? parseInt(selectedSubtitleId) : -1
        jellyfinBackend.get_playback_url(detail.itemId, detail.mediaSourceId || detail.itemId,
                                          audioStreamIdx, subStreamIdx)
    }

    // Starting mpv runs synchronously and, on the Pi, immediately switches VT
    // (suspending Qt's render thread) before the LOADING frame can paint. Defer
    // the launch one tick so the loading indicator is rendered first.
    Timer {
        id: startTimer
        interval: 50
        repeat: false
        property int pendingOffset: 0
        onTriggered: doStartPlayback(pendingOffset)
    }

    function beginPlayback(offsetMs) {
        startTimer.pendingOffset = offsetMs
        startTimer.restart()
    }

    function doStartPlayback(offsetMs) {
        // mpv audio/subtitle tracks are 1-based; -1 means auto-select.
        var audioTrack, subTrack
        if (isTranscoding) {
            // HLS manifest already contains the requested streams via
            // AudioStreamIndex/SubtitleStreamIndex. mpv just needs to
            // select the included subtitle track or disable them.
            audioTrack = -1
            subTrack   = selectedSubtitleId ? 1 : -1
        } else {
            // Direct play: map the selected Jellyfin stream index to mpv's
            // 1-based track numbers within each stream type.
            audioTrack = audioStreams.length > 0 ? audioIdx + 1 : 0
            subTrack   = subtitleIdx >= 0 ? subtitleIdx + 1 : -1
        }
        mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0, audioTrack, subTrack, [], false, -1, 0.0, "")
    }

    function formatTime(ms) {
        var s = Math.floor(ms / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        if (h > 0)
            return h + ":" + (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec
        return m + ":" + (sec < 10 ? "0" : "") + sec
    }

    Connections {
        target: jellyfinBackend
        function onErrorOccurred(msg) { console.log("[Jellyfin Player] Backend error: " + msg) }

        function onStreamUrlReady(url) {
            if (pendingNextEpisode) {
                // Stream URL for the auto-advanced next episode just arrived
                pendingNextEpisode = false
                playerRoot.streamUrl = url
                doStartPlayback(0)
                return
            }
        }

        function onNextEpisodeReady(detail) {
            if (!pendingNextEpisode) return
            // Empty detail → no next episode in the season
            if (!detail || !detail.itemId) {
                pendingNextEpisode = false
                goBack()
                return
            }
            playerRoot.advanceToEpisode(detail)
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) {
                playerRoot.lastKnownPositionMs = ms
                // First position update means mpv is up and playing — drop the
                // loading indicator (mpv's own window now covers the screen).
                playerRoot.playbackStarted = true
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }

        function onPlaybackFinished(finalPositionMs, finalDurationMs) {
            // mpv exited because the user quit/stopped — return to the detail view.
            reportStopped(finalPositionMs, finalDurationMs)
            goBack()
        }

        function onPlaybackFinishedNaturally(finalPositionMs, finalDurationMs) {
            // mpv reached the end of the file. Mark it stopped in Jellyfin,
            // then auto-advance to the next episode if the feature is enabled.
            reportStopped(finalPositionMs, finalDurationMs)
            if (!autoplayNext) { goBack(); return }
            pendingNextEpisode = true
            jellyfinBackend.load_next_episode(itemId)
        }

        function onPlaybackFailed() {
            // mpv exited with an error (e.g. DRM permissions, unsupported codec).
            // Report stopped so Jellyfin doesn't show this as still playing.
            // Mark as failed so the server doesn't update the resume position.
            if (lastKnownPositionMs > 0)
                reportStopped(lastKnownPositionMs, lastKnownDurationMs, true)
            goBack()
        }

    }

    Timer {
        interval: 10000
        repeat:   true
        running:  true
        onTriggered: {
            if (mpvController.position > 0)
                jellyfinBackend.update_playback_progress(itemId, mediaSourceId,
                                                         msToTicks(mpvController.position), false)
        }
    }

    Component.onCompleted: {
        initStreamIndices()
        if (streamUrl === "") return
        resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        // Match ModuleSettings.qml's reading of a toggle: stored as a real bool
        // once the user touches it, but accept the legacy "ON" string too.
        var autoplayRaw = appCore.get_setting(moduleRoot.moduleId, "autoplay_next_episode")
        autoplayNext = (autoplayRaw === true || autoplayRaw === "ON")

        if (resumeSkip) {
            // Continue Watching: always resume, no dialog
            beginPlayback(viewOffset)
        } else if (resumeSetting === "ask" && viewOffset > 0) {
            overlayVisible = true
        } else if (resumeSetting === "never") {
            beginPlayback(0)
        } else {
            beginPlayback(viewOffset)
        }
    }

    // Safety net: if the Player view is destroyed (e.g. app quit, back nav
    // without stopping), report stopped so Jellyfin doesn't show this as
    // still playing. The guard in reportStopped prevents double-reporting.
    Component.onDestruction: {
        if (lastKnownPositionMs > 0)
            reportStopped(lastKnownPositionMs, lastKnownDurationMs)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        // Shown while mpv launches and buffers the stream (before its window
        // takes over). Hidden once the first position update arrives, or while
        // the resume prompt is up.
        Text {
            text: "LOADING..."
            // White to match mpv's own overlay text color.
            color: "white"
            font.family: root.globalFont
            anchors.centerIn: parent
            font.pixelSize: root.sh * 0.05 //24
            visible: streamUrl !== "" && !overlayVisible && !playbackStarted
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: overlayVisible

        Rectangle {
            id: dialogRect
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875
            height: root.sh * 0.2833333

            Column {
                id: dialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05

                Text {
                    text: "RESUME PLAYBACK?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: [
                            "Resume from " + formatTime(viewOffset),
                            "Start from the beginning"
                        ]
                        delegate: Item {
                            width: dialogColumn.width
                            height: root.sh * 0.0583333

                            Rectangle {
                                anchors.fill: delegateText
                                color: root.accentColor
                                visible: index === choiceIndex
                            }

                            Text {
                                id: delegateText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData
                                color: index === choiceIndex ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.capitalization: Font.AllUppercase
                                topPadding: root.sh * 0.0041667
                                leftPadding: root.sw * 0.009375
                                rightPadding: root.sw * 0.009375
                                bottomPadding: root.sh * 0.00625
                                font.pixelSize: root.sh * 0.0416667
                            }
                        }
                    }
                }

                Text {
                    text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }
}
