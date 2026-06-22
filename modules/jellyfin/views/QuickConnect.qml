import QtQuick
import Components

FocusScope {
    id: qcRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal replaceWith(string path, var params)
    signal goBack()

    property string serverUrl: navParams.serverUrl || ""
    property string code: navParams.code || ""
    property string secret: navParams.secret || ""
    property bool approved: false
    property bool failed: false
    property string errorMsg: ""
    property int pollCount: 0
    property bool polling: false

    // When approved, authenticate and transition to libraries
    Connections {
        target: jellyfinBackend

        function onQuickConnectCodeReady(c, s) {
            qcRoot.code = c
            qcRoot.secret = s
            qcRoot.polling = true
            pollTimer.restart()
        }

        function onQuickConnectApproved() {
            qcRoot.polling = false
            pollTimer.stop()
            qcRoot.approved = true
            // Exchange the quick connect secret for an access token
            jellyfinBackend.quick_connect_authenticate(qcRoot.secret)
        }

        function onQuickConnectFailed(msg) {
            qcRoot.polling = false
            pollTimer.stop()
            qcRoot.failed = true
            qcRoot.errorMsg = msg
        }

        function onAuthStateChanged() {
            if (jellyfinBackend.get_auth_state() === "authed") {
                // Save server_url then transition to libraries
                appCore.save_setting(moduleRoot.moduleId, "server_url", qcRoot.serverUrl)
                qcRoot.replaceWith("Libraries.qml", {})
            }
        }
    }

    Component.onCompleted: {
        if (serverUrl === "") {
            errorMsg = "NO SERVER URL"
            failed = true
            return
        }
        // If code/secret already provided from Auth.qml, start polling
        if (qcRoot.code !== "" && qcRoot.secret !== "") {
            qcRoot.polling = true
            pollTimer.restart()
            return
        }
        // Otherwise initiate a new quick connect
        jellyfinBackend.quick_connect_initiate(serverUrl)
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace ||
            event.key === Qt.Key_Back) {
            jellyfinBackend.quick_connect_cancel()
            goBack()
            event.accepted = true
        }
    }

    // Poll every 2 seconds
    Timer {
        id: pollTimer
        interval: 2000
        repeat: true
        onTriggered: {
            if (qcRoot.secret !== "" && !approved && !failed) {
                pollCount++
                jellyfinBackend.quick_connect_poll(qcRoot.secret)
                // Give up after 60 polls (2 minutes)
                if (pollCount >= 60) {
                    polling = false
                    stop()
                    failed = true
                    errorMsg = "CODE EXPIRED"
                }
            }
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Quick Connect"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333  // 16
        width: root.sw * 0.75  // 480

        // Code display (when active)
        Rectangle {
            visible: !failed && code !== ""
            width: root.sw * 0.5  // 320
            height: root.sh * 0.1666667  // 80
            color: root.surfaceColor
            border.color: root.accentColor
            border.width: root.sh * 0.0041667  // 2
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                anchors.centerIn: parent
                text: qcRoot.code
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0833333  // 40
                font.letterSpacing: root.sw * 0.03125  // 20
            }
        }

        // Instructions
        Text {
            visible: !failed && code !== ""
            text: "Open a browser and go to:\n" + serverUrl + "/web/#/quickconnect"
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333  // 16
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.sw * 0.625  // 400
            wrapMode: Text.WordWrap
            opacity: 0.8
        }

        Text {
            visible: !failed && code !== ""
            text: "Enter the code shown above"
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333  // 16
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Status
        Text {
            visible: !failed
            text: approved ? "APPROVED — SIGNING IN..."
                          : (code !== "" ? "WAITING FOR APPROVAL..."
                                         : "CONNECTING...")
            color: approved ? root.accentColor : root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0333333  // 16
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Error state
        Rectangle {
            visible: failed
            width: root.sw * 0.5  // 320
            height: root.sh * 0.0583333  // 28
            color: root.surfaceColor
            border.color: root.accentColor
            border.width: root.sh * 0.003125  // 2
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                anchors.centerIn: parent
                text: qcRoot.errorMsg
                color: root.accentColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0375  // 18
            }
        }

        // Cancel / Retry hint
        Text {
            visible: failed
            text: root.hints.back + ":RETRY"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0333333  // 16
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    // Footer
    Text {
        text: root.hints.back + ":CANCEL"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667  // 50
        anchors.leftMargin: root.sw * 0.125  // 80
        font.pixelSize: root.sh * 0.0333333  // 16
    }
}
