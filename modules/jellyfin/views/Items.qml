import QtQuick
import Components

// Reusable list browser — handles movies, series, and episodes via navParams.
// Follows the Plex Items.qml pattern: 28px rows, 24px font, marquee on select.
FocusScope {
    id: itemListRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string parentId: navParams.parentId || ""
    property string listTitle: navParams.title || ""
    property string libraryName: navParams.libraryName || ""
    property string includeTypes: navParams.includeTypes || "Movie"
    property string mode: navParams.mode || "browse"   // "browse", "resume", or "up_next"

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    Connections {
        target: jellyfinBackend

        function onItemsLoaded(loadedItems) {
            if (itemListRoot.mode !== "browse") return
            itemListRoot.isLoading = false
            itemListRoot.items = loadedItems
            if (loadedItems.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                itemList.currentIndex = Math.min(restore, loadedItems.length - 1)
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            }
        }

        function onContinueWatchingLoaded(loadedItems) {
            if (itemListRoot.mode !== "resume") return
            itemListRoot.isLoading = false
            itemListRoot.items = loadedItems
            if (loadedItems.length > 0) {
                itemList.currentIndex = 0
                itemList.positionViewAtIndex(0, ListView.Contain)
            }
        }

        function onUpNextLoaded(loadedItems) {
            if (itemListRoot.mode !== "up_next") return
            itemListRoot.isLoading = false
            itemListRoot.items = loadedItems
            if (loadedItems.length > 0) {
                itemList.currentIndex = 0
                itemList.positionViewAtIndex(0, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            itemListRoot.isLoading = false
            itemListRoot.errorMessage = msg
            console.log("[Jellyfin Items] Error: " + msg)
        }
    }

    Component.onCompleted: {
        if (mode === "resume") {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_continue_watching()
        } else if (mode === "up_next") {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_up_next()
        } else {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_items(parentId, includeTypes, "SortName")
        }
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: listTitle
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Loading / empty / error states
    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage !== ""
        text: errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage === "" && items.length === 0
        text: "NO ITEMS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body — Plex-style text list
    ListView {
        id: itemList
        model: items
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onReturnPressed: itemListRoot.selectItem()

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, itemList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: itemList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: {
                        var base = (modelData.type === "episode" && modelData.grandparentTitle)
                                   ? (modelData.grandparentTitle + ": " + (modelData.title || ""))
                                   : (modelData.title || "")
                        return modelData.year ? base + " · " + String(modelData.year) : base
                    }
                    color: itemList.currentIndex === index
                       ? root.surfaceColor : root.primaryColor
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
                    running: (itemList.currentIndex === index) &&
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

    function selectItem() {
        var item = items[itemList.currentIndex]
        if (!item) return

        if (item.type === "series") {
            itemListRoot.navigateTo("ItemShow.qml", { item: item, libraryName: libraryName }, { currentIndex: itemList.currentIndex })
        } else {
            itemListRoot.navigateTo("Item.qml", { item: item, libraryName: libraryName },
                                   { currentIndex: itemList.currentIndex })
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
