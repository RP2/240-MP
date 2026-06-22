import QtQuick
import Components

FocusScope {
    id: seasonRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var item: navParams.item || {}

    property var seasons: []
    property bool isLoading: false

    Connections {
        target: jellyfinBackend

        function onSeasonsLoaded(loadedItems) {
            seasonRoot.isLoading = false
            seasonRoot.seasons = loadedItems
            // [dev] console.log("[ItemSeason] onSeasonsLoaded: " + loadedItems.length + " seasons")
            if (loadedItems.length > 0) seasonList.currentIndex = 0
        }

        function onEpisodesLoaded(loadedItems) {
            // [dev] console.log("[ItemSeason] onEpisodesLoaded: " + loadedItems.length + " episodes")
            seasonRoot.navigateTo("Items.qml", {
                mode: "episodes",
                title: "Season " + seasons[seasonList.currentIndex].index,
                items: loadedItems
            }, { currentIndex: seasonList.currentIndex })
        }

        function onErrorOccurred(msg) {
            seasonRoot.isLoading = false
            console.log("[Jellyfin Season] Error: " + msg)
        }
    }

    Component.onCompleted: {
        isLoading = true
        if (item.itemId) jellyfinBackend.load_seasons(item.itemId)
    }

    focus: true

    Keys.onUpPressed: if (seasonList.currentIndex > 0) seasonList.currentIndex--
    Keys.onDownPressed: if (seasonList.currentIndex < seasonList.count - 1) seasonList.currentIndex++
    Keys.onReturnPressed: {
        var season = seasons[seasonList.currentIndex]
        if (!season) return
        jellyfinBackend.load_episodes(item.itemId, season.itemId)
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: item.title || ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    Item {
        visible: !isLoading
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true

        Text {
            id: seasonListLabel
            text: "Seasons:"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: root.sh * 0.0145833 //7
            leftPadding: root.sw * 0.009375 //6
            rightPadding: root.sw * 0.009375 //6
            font.pixelSize: root.sh * 0.0291667 //14
        }

        ListView {
            id: seasonList
            model: seasons
            anchors.top: seasonListLabel.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: root.sh * 0.0145833 //7
            height: root.sh * 0.3333 //160
            clip: true
            focus: true

            delegate: Item {
                width: seasonList.width
                height: root.sh * 0.0583333 //28

                Item {
                    id: textClip
                    width: Math.min(rowText.implicitWidth, seasonList.width)
                    height: parent.height
                    clip: true

                    Rectangle {
                        color: root.accentColor
                        anchors.fill: rowText
                        visible: seasonList.currentIndex === index
                    }

                    Text {
                        id: rowText
                        text: {
                            var label = modelData.title || ("Season " + modelData.index)
                            var count = modelData.leafCount || 0
                            if (count > 0) label += " (" + count + " episodes)"
                            return label
                        }
                        color: seasonList.currentIndex === index ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        anchors.verticalCenter: parent.verticalCenter
                        x: 0
                        topPadding: root.sh * 0.0041667 //2
                        leftPadding: root.sw * 0.009375 //6
                        rightPadding: root.sw * 0.009375 //6
                        bottomPadding: root.sh * 0.00625 //3
                        font.pixelSize: root.sh * 0.05 //24
                    }

                    SequentialAnimation {
                        running: (seasonList.currentIndex === index) &&
                                 (rowText.implicitWidth > textClip.width)
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) rowText.x = 0
                        PauseAnimation { duration: 1500 }
                        NumberAnimation {
                            target: rowText; property: "x"
                            to: textClip.width - rowText.implicitWidth
                            duration: Math.abs(to) * 20
                        }
                        PauseAnimation { duration: 2000 }
                        PropertyAction { target: rowText; property: "x"; value: 0 }
                    }
                }
            }
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
