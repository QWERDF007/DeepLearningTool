import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

DltComboBox {
    id: control

    normalColor: DltColor.Button
    // 公共属性
    property LabelClassesModel labelClassesModel: null
    property int currentClassId: -1
    property bool showMultipleDifferent: false  // 是否显示"不同类别"状态
    property string multipleDifferentText: "不同类别"  // 多个不同类别时的显示文本

    // 信号
    signal classChanged(int newClassId)

    // 内部状态
    property bool _updating: false
    property color _currentColor: DltColor.Transparent

    implicitWidth: 200
    implicitHeight: 32

    model: labelClassesModel
    textRole: "name"

    // 显示文本：如果是多个不同类别，显示自定义文本
    displayText: {
        if (showMultipleDifferent) {
            return multipleDifferentText
        }
        if (currentIndex >= 0 && labelClassesModel) {
            let modelIndex = labelClassesModel.index(currentIndex, 0)
            return labelClassesModel.data(modelIndex, LabelClassesModel.NameRole)
        }
        return ""
    }

    Component.onCompleted: {
        syncCurrentIndex()
    }

    // 当 currentClassId 从外部改变时更新 currentIndex
    onCurrentClassIdChanged: {
        syncCurrentIndex()
    }

    // 同步 currentIndex 与 currentClassId
    function syncCurrentIndex() {
        if (_updating || !labelClassesModel) return
        _updating = true
        
        // 查找给定 classId 的索引
        var rowCount = labelClassesModel.rowCount()
        for (var i = 0; i < rowCount; i++) {
            var modelIndex = labelClassesModel.index(i, 0)
            var classId = labelClassesModel.data(modelIndex, LabelClassesModel.LabelClassIdRole)
            if (classId === currentClassId) {
                currentIndex = i
                var colorData = labelClassesModel.data(modelIndex, LabelClassesModel.ColorRole)
                _currentColor = colorData ? colorData : DltColor.Transparent
                _updating = false
                return
            }
        }
        
        // 未找到，重置为 -1
        currentIndex = -1
        _currentColor = DltColor.Transparent
        _updating = false
    }

    // 当用户选择不同的类别时发出 classChanged 信号
    onActivated: function(index) {
        if (_updating || !labelClassesModel || index < 0) return
        
        var modelIndex = labelClassesModel.index(index, 0)
        var newClassId = labelClassesModel.data(modelIndex, LabelClassesModel.LabelClassIdRole)
        
        if (newClassId !== currentClassId) {
            var colorData = labelClassesModel.data(modelIndex, LabelClassesModel.ColorRole)
            _currentColor = colorData ? colorData : DltColor.Transparent
            // 不在这里修改 currentClassId - 让父组件处理
            classChanged(newClassId)
        }
    }

    // 自定义委托以显示颜色和名称
    delegate: ItemDelegate {
        id: delegateItem
        width: ListView.view ? ListView.view.width : control.width
        height: 32
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled

        property color delegateColor: model.color !== undefined ? model.color : DltColor.Transparent

        contentItem: RowLayout {
            spacing: 8
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            // 颜色指示器
            Rectangle {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                radius: 3
                color: delegateItem.delegateColor
                border.width: 1
                border.color: Qt.darker(delegateItem.delegateColor, 1.3)
            }

            // 类别名称
            DltText {
                text: model.name !== undefined ? model.name : ""
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        background: Rectangle {
            color: delegateItem.highlighted ? DltColor.Hovered : DltColor.Transparent
        }
    }

    // 自定义内容项以显示选中的类别及其颜色
    contentItem: Item {
        implicitHeight: 32

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: control.indicator ? control.indicator.width + 8 : 8
            spacing: 8

            // 选中类别的颜色指示器
            Rectangle {
                id: colorIndicator
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                radius: 3
                visible: !control.showMultipleDifferent && control.currentIndex >= 0 && control.labelClassesModel
                color: control._currentColor
                border.width: visible ? 1 : 0
                border.color: visible && control._currentColor !== DltColor.Transparent ? Qt.darker(control._currentColor, 1.3) : DltColor.Transparent
            }

            // 显示文本
            DltText {
                text: control.displayText
                Layout.fillWidth: true
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
