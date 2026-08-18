import QtQuick
import QtQuick.Layouts

import quickui

LabelClassEditorBase {
    id: control

    extraFieldsHeight: 50
    defaultClassGroup: "anomaly"
    property bool allowUnlabeledGroup: true

    onClassGroupChanged: syncGroupBox()
    onAllowUnlabeledGroupChanged: {
        if (!allowUnlabeledGroup && normalizedGroup(classGroup) === "unlabeled") {
            classGroup = "anomaly"
            return
        }
        syncGroupBox()
    }

    function normalizedGroup(group) {
        if (group === "unlabeled" || group === "未标注") {
            return "unlabeled"
        }
        if (group === "good" || group === "良好" || group === "正常" || group === "ok") {
            return "good"
        }
        return "anomaly"
    }

    function groupIndex(group) {
        let normalized = normalizedGroup(group)
        if (normalized === "unlabeled" && allowUnlabeledGroup) {
            return 0
        }
        if (normalized === "good") {
            return allowUnlabeledGroup ? 1 : 0
        }
        return allowUnlabeledGroup ? 2 : 1
    }

    function groupFromIndex(index) {
        if (allowUnlabeledGroup && index === 0) {
            return "unlabeled"
        }
        if (index === (allowUnlabeledGroup ? 1 : 0)) {
            return "good"
        }
        return "anomaly"
    }

    function normalizeClassGroup(group) {
        return normalizedGroup(group)
    }

    function syncGroupBox() {
        let idx = groupIndex(classGroup)
        if (groupBox.currentIndex !== idx) {
            groupBox.currentIndex = idx
        }
    }

    QuiText {
        text: "分组"
    }

    QuiComboBox {
        id: groupBox
        Layout.fillWidth: true
        model: control.allowUnlabeledGroup ? ["未标注", "正常", "异常"] : ["正常", "异常"]
        currentIndex: control.groupIndex(control.classGroup)
        onActivated: function(index) {
            control.classGroup = control.groupFromIndex(index)
            control.notifyChanged()
        }
    }
}
