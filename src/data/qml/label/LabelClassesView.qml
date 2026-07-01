import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window

import dltool.data
import dltool.ui
import quickui

Rectangle {
    id: labelClassesView
    clip: true
    width: 200
    height: 200
    color: QuiColor.Primary
    focus: true
    activeFocusOnTab: true
    property DataManager dataManager
    property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null
    property ItemSelectionModel selection: dataManager ? dataManager.labelClasses.selection : null
    property int _label_class_id: -1

    // 点击获取焦点
    MouseArea {
        anchors.fill: parent
        onClicked: {
            labelClassesView.forceActiveFocus()
        }
        // 允许事件传递给子组件
        propagateComposedEvents: true
        onPressed: function(mouse) {
            mouse.accepted = false
        }
        onReleased: function(mouse) {
            mouse.accepted = false
        }
    }

    // 键盘事件处理 - 快捷键选中
    Keys.onPressed: function(event) {
        if (!labelClasses || !selection) {
            return
        }
        
        let key = event.text
        if (key.length === 0) {
            return
        }
        
        let matchIndex = labelClasses.findByShortcut(key)
        if (matchIndex >= 0) {
            let modelIndex = labelClasses.index(matchIndex, 0)
            selection.select(modelIndex, ItemSelectionModel.ClearAndSelect)
            selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
            event.accepted = true
        }
    }

    LabelClassEditor {
        id: editor
        isCreate: false
        maxOrdinalIndex: view.count
        onLabelClassChanged: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (dataManager) {
                let nameMsg = dataManager.isValidClassName(className, classId)
                if (nameMsg.length > 0) {
                    editor.msg = nameMsg
                    return
                }
            }
            if (labelClasses) {
                editor.msg = labelClasses.isValid(classId, className, classShortcut, ordinalIndex)
            }
        }
        onLabelClassChangedAccepted: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (dataManager && dataManager.isValidClassName(className, classId).length === 0) {
                dataManager.updateLabelClass(classId, className, classColor, classShortcut, ordinalIndex)
            }
        }
    }

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除标签类别"
        message: "确定删除选中的标签类别吗?"
        usePositiveButton: true
        useNegativeButton: true
        onPositiveClicked: function () {
            if (dataManager) {
                dataManager.deleteLabelClass(_label_class_id)
            }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        
        LabelClassesHeader {
            Layout.fillWidth: true
            height: 32
            dataManager: labelClassesView.dataManager
        }
        ListView {
            id: view
            clip: true
            spacing: 5
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: QuiScrollBar {}
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: labelClassesView.labelClasses
            
            // 添加位移动画
            displaced: Transition {
                NumberAnimation { properties: "x,y"; duration: 200; easing.type: Easing.OutQuad }
            }
            
            // 添加移动动画
            move: Transition {
                NumberAnimation { properties: "x,y"; duration: 200; easing.type: Easing.OutQuad }
            }
            
            delegate:  LabelClassDelegate {
                width: view.width - 8
                height: 32
                backgroundColor: model.selected ? QuiColor.Highlight : Qt.lighter(QuiColor.Primary, 1.2)
                className: model.name
                classColor: model.color
                classShortcut: model.shortcut
                classId: model.label_class_id
                ordinalIndex: model.ordinal_index
                listView: view
                labelClasses: labelClassesView.labelClasses
                onClicked: function() {
                    let tmpIndex = labelClasses.index(index, 0)
                    selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                    selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                }
                onEditClicked: function() {
                    let pos = mapToItem(null, 0, 0)
                    let windowWidth = Window.width
                    let windowHeight = Window.height
                    let editorWidth = editor.width
                    let editorHeight = editor.height
                    
                    // 计算初始位置（按钮右侧）
                    let targetX = pos.x + width
                    let targetY = pos.y + 10
                    
                    // 水平边界检查：如果右侧超出窗口，则显示在左侧
                    if (targetX + editorWidth > windowWidth) {
                        targetX = pos.x - editorWidth
                        if (targetX < 0) {
                            targetX = 0
                        }
                    }
                    
                    // 垂直边界检查：确保弹窗不超出窗口底部
                    if (targetY + editorHeight > windowHeight) {
                        targetY = windowHeight - editorHeight
                    }
                    // 确保不超出窗口顶部
                    if (targetY < 0) {
                        targetY = 0
                    }
                    
                    editor.x = targetX
                    editor.y = targetY
                    editor.classId = model.label_class_id
                    editor.className = model.name
                    editor.classColor = model.color
                    editor.classShortcut = model.shortcut
                    editor.ordinalIndex = model.ordinal_index
                    editor.open()
                }
                onDeleteClicked: function () {
                    _label_class_id = model.label_class_id
                    deleteConfirmDialog.open()
                }
            }
        }
    }
}
