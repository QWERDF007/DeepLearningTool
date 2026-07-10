import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.tool
import dltool.project
import quickui

ApplicationWindow {
    id: window
    visible: true
    width: Screen.width
    height: Screen.height
    title: ProjectManager.currentProject ? ProjectManager.currentProject.path : ""

    Connections {
        target: ProjectManager
        function onProjectActivated() {
            if (ProjectManager.currentProject) {
                _header.currentIndex = 1
            }
        }
        function onCurrentProjectChanged() {
            if (!ProjectManager.currentProject) {
                _header.currentIndex = 0
            }
        }
    }

    header: Header {
        id: _header
        width: parent.width
        // height: 48
    }


    Content {
        id: content
        anchors {
            top: parent.top
            bottom: parent.bottom
        }
        height: parent.heght
        width: parent.width
        Layout.fillHeight: true
        Layout.fillWidth: true
        currentIndex: _header.currentIndex
    }

    QuiInfoBar {
        id: infoBar
        root: window
        layoutY: _header.height + 12
    }

    Connections {
        target: SignalHelper
        function onSuccess(title, message, duration) {
            infoBar.showSuccess(title || "", duration, message || "")
        }
        function onInfo(title, message, duration) {
            infoBar.showInfo(title || "", duration, message || "")
        }
        function onWarn(title, message, duration) {
            infoBar.showWarning(title || "", duration, message || "")
        }
        function onError(title, message, duration) {
            infoBar.showError(title || "", duration, message || "")
        }
    }


    footer: Footer {
        id: _footer
        width: parent.width
        height: 32
    }

    Component.onCompleted: {
        window.showMaximized()
    }

    onClosing: function (close){
        close.accepted = false
        exitDialog.open()
    }

    QuiContentDialog {
        id: exitDialog
        title: "退出程序"
        message: "确定退出程序吗?"
        positiveText: "退出"
        negativeText: "取消"
        onPositiveClicked: function () {
            content.shuttingDown = true
            Qt.callLater(function () {
                Qt.exit(0)
            })
        }
    }
}
