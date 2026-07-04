import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window

import dltool.core
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
    readonly property var imageInstances: dataManager ? dataManager.imageInstances : null
    readonly property var imageLabelsList: dataManager ? dataManager.imageLabelsList : null
    readonly property bool classificationMode: dataManager ? dataManager.method === DeepLearningMethod.Classification : false
    property int _label_class_id: -1
    property bool syncingClassSelection: false

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
            selectClassIndex(matchIndex, true)
            event.accepted = true
        }
    }

    onClassificationModeChanged: syncSelectionToCurrentImageClass()
    onDataManagerChanged: syncSelectionToCurrentImageClass()

    Connections {
        target: labelClassesView.imageInstances
        function onCurrentImageChanged() {
            labelClassesView.syncSelectionToCurrentImageClass()
        }
    }

    Connections {
        target: labelClassesView.imageLabelsList
        function onRowsInserted(parent, first, last) { labelClassesView.syncSelectionToCurrentImageClass() }
        function onRowsRemoved(parent, first, last) { labelClassesView.syncSelectionToCurrentImageClass() }
        function onDataChanged(topLeft, bottomRight, roles) { labelClassesView.syncSelectionToCurrentImageClass() }
        function onModelReset() { labelClassesView.syncSelectionToCurrentImageClass() }
    }

    Connections {
        target: labelClassesView.selection
        function onCurrentChanged(current, previous) {
            if (!labelClassesView.classificationMode || labelClassesView.syncingClassSelection) {
                return
            }

            let row = current ? current.row : -1
            if (row >= 0) {
                labelClassesView.applyClassToCurrentImage(labelClassesView.classIdAt(row))
            }
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
                editor.msg = labelClasses.isValid(classId, className, classColor, classShortcut, ordinalIndex)
            }
        }
        onLabelClassChangedAccepted: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (dataManager
                    && dataManager.isValidClassName(className, classId).length === 0
                    && (!labelClasses || !labelClasses.isValid(classId, className, classColor, classShortcut, ordinalIndex).startsWith("error:"))) {
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
            labelClasses: labelClassesView.labelClasses
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
                    labelClassesView.selectClassIndex(index, true)
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

    function selectClassIndex(row, applyToImage) {
        if (!labelClasses || !selection || row < 0 || row >= labelClasses.rowCount()) {
            return false
        }

        let modelIndex = labelClasses.index(row, 0)
        let wasSyncing = syncingClassSelection
        syncingClassSelection = true
        selection.select(modelIndex, ItemSelectionModel.ClearAndSelect)
        selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
        syncingClassSelection = wasSyncing

        if (applyToImage && classificationMode) {
            applyClassToCurrentImage(classIdAt(row))
        }
        return true
    }

    function syncSelectionToCurrentImageClass() {
        if (!classificationMode || !labelClasses || !selection) {
            return
        }

        let classId = currentImageClassId()
        syncingClassSelection = true
        if (classId >= 0) {
            let row = findClassRow(classId)
            if (row >= 0) {
                selectClassIndex(row, false)
            } else {
                selection.clear()
            }
        } else {
            selection.clear()
        }
        syncingClassSelection = false
    }

    function applyClassToCurrentImage(classId) {
        if (!classificationMode || !dataManager || !imageInstances || classId < 0) {
            return false
        }

        let imageId = Number(imageInstances.currentImageId ?? -1)
        if (imageId < 0) {
            return false
        }

        let labelData = currentImageLabelData()
        let labelId = labelData ? Number(labelData.label_id ?? -1) : -1
        let currentClassId = labelData ? Number(labelData.label_class_id ?? -1) : -1

        if (labelId >= 0) {
            if (currentClassId !== classId) {
                dataManager.updateLabelsClass([labelId], [classId])
            }
            return true
        }

        return dataManager.addLabel(imageId, classId, {})
    }

    function currentImageClassId() {
        let labelData = currentImageLabelData()
        return labelData ? Number(labelData.label_class_id ?? -1) : -1
    }

    function currentImageLabelData() {
        if (!imageLabelsList || imageLabelsList.rowCount() <= 0) {
            return null
        }
        return imageLabelsList.getData(0)
    }

    function classIdAt(row) {
        if (!labelClasses || row < 0 || row >= labelClasses.rowCount()) {
            return -1
        }
        let modelIndex = labelClasses.index(row, 0)
        return Number(labelClasses.data(modelIndex, LabelClassesModel.LabelClassIdRole) ?? -1)
    }

    function findClassRow(classId) {
        if (!labelClasses || classId < 0) {
            return -1
        }

        for (let row = 0; row < labelClasses.rowCount(); ++row) {
            if (classIdAt(row) === classId) {
                return row
            }
        }
        return -1
    }

    Component.onCompleted: syncSelectionToCurrentImageClass()
}
