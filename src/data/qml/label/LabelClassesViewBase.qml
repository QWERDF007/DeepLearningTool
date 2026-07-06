import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    property ItemSelectionModel selection: labelClasses ? labelClasses.selection : null
    readonly property var imageInstances: dataManager ? dataManager.imageInstances : null
    readonly property var imageLabelsList: dataManager ? dataManager.imageLabelsList : null
    property bool selectionFollowsCurrentImageClass: false
    property var viewModel: labelClasses
    property Component rowDelegateComponent: defaultRowDelegateComponent
    property Component editorComponent: defaultEditorComponent
    property alias listView: view
    property string createDefaultGroup: "anomaly"
    property int _label_class_id: -1
    property bool syncingClassSelection: false

    Component {
        id: defaultEditorComponent
        LabelClassEditorBase {}
    }

    Component {
        id: defaultRowDelegateComponent
        LabelClassDelegateBase {
            width: view.width - 8
            height: 32
            backgroundColor: model.selected ? QuiColor.Highlight : Qt.lighter(QuiColor.Primary, 1.2)
            className: model.name || ""
            classColor: model.color || "black"
            classShortcut: model.shortcut || ""
            classId: Number(model.label_class_id)
            ordinalIndex: Number(model.ordinal_index)
            listView: view
            labelClasses: labelClassesView.labelClasses
            dragEnabled: true
            onClicked: labelClassesView.selectClassIndex(index, true)
            onEditClicked: labelClassesView.openEditorForClass(this, Number(model.label_class_id),
                                                               model.name || "", model.color || "black",
                                                               model.shortcut || "", Number(model.ordinal_index),
                                                               model.group || "anomaly")
            onDeleteClicked: {
                labelClassesView.confirmDeleteClass(Number(model.label_class_id))
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: labelClassesView.forceActiveFocus()
        propagateComposedEvents: true
        onPressed: function(mouse) { mouse.accepted = false }
        onReleased: function(mouse) { mouse.accepted = false }
    }

    Keys.onPressed: function(event) {
        if (handleShortcutEvent(event)) {
            event.accepted = true
        }
    }

    onSelectionFollowsCurrentImageClassChanged: syncSelectionToCurrentImageClass()
    onDataManagerChanged: {
        refreshViewModel()
        syncSelectionToCurrentImageClass()
    }
    onLabelClassesChanged: {
        refreshViewModel()
        syncSelectionToCurrentImageClass()
    }

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
        target: labelClassesView.labelClasses
        function onRowsInserted(parent, first, last) {
            labelClassesView.refreshViewModel()
            labelClassesView.syncSelectionToCurrentImageClass()
        }
        function onRowsRemoved(parent, first, last) {
            labelClassesView.refreshViewModel()
            labelClassesView.syncSelectionToCurrentImageClass()
        }
        function onDataChanged(topLeft, bottomRight, roles) {
            labelClassesView.refreshViewModel()
            labelClassesView.syncSelectionToCurrentImageClass()
        }
        function onModelReset() {
            labelClassesView.refreshViewModel()
            labelClassesView.syncSelectionToCurrentImageClass()
        }
    }

    Connections {
        target: labelClassesView.selection
        function onCurrentChanged(current, previous) {
            if (!labelClassesView.selectionFollowsCurrentImageClass || labelClassesView.syncingClassSelection) {
                return
            }

            let row = current ? current.row : -1
            if (row >= 0) {
                labelClassesView.applyClassToCurrentImage(labelClassesView.classIdAt(row))
            }
        }
    }

    Connections {
        target: editorLoader.item
        function onLabelClassChanged(classId, className, classColor, classShortcut, ordinalIndex, classGroup) {
            labelClassesView.validateEditorInput(classId, className, classColor, classShortcut, ordinalIndex)
        }
        function onLabelClassChangedAccepted(classId, className, classColor, classShortcut, ordinalIndex, classGroup) {
            if (!labelClassesView.isEditorInputValid(classId, className, classColor, classShortcut, ordinalIndex)) {
                return
            }
            if (editorLoader.item && editorLoader.item.isCreate) {
                labelClassesView.addLabelClass(className, classColor, classShortcut, classGroup)
            } else {
                labelClassesView.updateLabelClass(classId, className, classColor, classShortcut, ordinalIndex,
                                                  classGroup)
            }
        }
    }

    Loader {
        id: editorLoader
        sourceComponent: labelClassesView.editorComponent
    }

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除标签类别"
        message: "确定删除选中的标签类别吗?"
        usePositiveButton: true
        useNegativeButton: true
        onPositiveClicked: function () {
            if (dataManager && labelClassesView._label_class_id >= 0) {
                dataManager.deleteLabelClass(labelClassesView._label_class_id)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5

        RowLayout {
            Layout.fillWidth: true
            height: 32

            QuiText {
                text: "标签类别:"
                font: QuiFont.Subtitle
            }

            Item {
                Layout.fillWidth: true
            }

            QuiTextIconButton {
                id: addButton
                iconSource: QuiFontIcon.Add
                text: "添加标签类别"
                onClicked: labelClassesView.openEditorForCreate(addButton)
            }
        }

        ListView {
            id: view
            clip: true
            spacing: 5
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: QuiScrollBar {}
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: labelClassesView.viewModel
            delegate: labelClassesView.rowDelegateComponent

            displaced: Transition {
                NumberAnimation { properties: "x,y"; duration: 200; easing.type: Easing.OutQuad }
            }

            move: Transition {
                NumberAnimation { properties: "x,y"; duration: 200; easing.type: Easing.OutQuad }
            }
        }
    }

    function usedColors() {
        let used = []
        if (!labelClasses) {
            return used
        }

        for (let row = 0; row < labelClasses.rowCount(); ++row) {
            let modelIndex = labelClasses.index(row, 0)
            let color = labelClasses.data(modelIndex, LabelClassesModel.ColorRole)
            if (color) {
                used.push(color)
            }
        }
        return used
    }

    function nextRecommendedColor() {
        return Utils.nextRecommendedColor(usedColors())
    }

    function editorMaxOrdinalIndex() {
        return labelClasses ? labelClasses.rowCount() : 0
    }

    function placeEditor(anchorItem) {
        let editor = editorLoader.item
        if (!editor || !anchorItem) {
            return
        }

        let pos = anchorItem.mapToItem(null, 0, 0)
        let editorWidth = editor.width
        let editorHeight = editor.height
        let targetX = pos.x + anchorItem.width
        let targetY = pos.y + 10

        if (targetX + editorWidth > Window.width) {
            targetX = pos.x - editorWidth
            if (targetX < 0) {
                targetX = 0
            }
        }
        if (targetY + editorHeight > Window.height) {
            targetY = Window.height - editorHeight
        }
        if (targetY < 0) {
            targetY = 0
        }

        editor.x = targetX
        editor.y = targetY
    }

    function openEditorForCreate(anchorItem) {
        let editor = editorLoader.item
        if (!editor) {
            return
        }

        placeEditor(anchorItem)
        editor.maxOrdinalIndex = editorMaxOrdinalIndex()
        editor.openForCreate(nextRecommendedColor(), createDefaultGroup)
    }

    function openEditorForClass(anchorItem, classId, className, classColor, classShortcut, ordinalIndex, classGroup) {
        let editor = editorLoader.item
        if (!editor) {
            return
        }

        placeEditor(anchorItem)
        editor.classId = classId
        editor.className = className
        editor.classColor = classColor
        editor.classShortcut = classShortcut
        editor.ordinalIndex = ordinalIndex
        editor.classGroup = classGroup
        editor.isCreate = false
        editor.maxOrdinalIndex = editorMaxOrdinalIndex()
        editor.open()
    }

    function confirmDeleteClass(classId) {
        _label_class_id = classId
        deleteConfirmDialog.open()
    }

    function validateEditorInput(classId, className, classColor, classShortcut, ordinalIndex) {
        let editor = editorLoader.item
        if (!editor) {
            return ""
        }

        let msg = ""
        if (dataManager) {
            msg = dataManager.isValidClassName(className, classId)
        }
        if (msg.length === 0 && labelClasses) {
            msg = labelClasses.isValid(classId, className, classColor, classShortcut, ordinalIndex)
        }
        editor.msg = msg
        return msg
    }

    function isEditorInputValid(classId, className, classColor, classShortcut, ordinalIndex) {
        let msg = validateEditorInput(classId, className, classColor, classShortcut, ordinalIndex)
        return msg.length === 0 || !msg.startsWith("error:")
    }

    function refreshViewModel() {
    }

    function addLabelClass(className, classColor, classShortcut, classGroup) {
        if (dataManager) {
            dataManager.addLabelClass(className, classColor, classShortcut)
        }
    }

    function updateLabelClass(classId, className, classColor, classShortcut, ordinalIndex, classGroup) {
        if (dataManager) {
            dataManager.updateLabelClass(classId, className, classColor, classShortcut, ordinalIndex)
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

        if (applyToImage && selectionFollowsCurrentImageClass) {
            applyClassToCurrentImage(classIdAt(row))
        }
        return true
    }

    function syncSelectionToCurrentImageClass() {
        if (!selectionFollowsCurrentImageClass || !labelClasses || !selection) {
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

    function currentImageClassId() {
        let labelData = currentImageLabelData()
        return labelData && labelData.label_class_id !== undefined ? Number(labelData.label_class_id) : -1
    }

    function applyClassToCurrentImage(classId) {
        if (!selectionFollowsCurrentImageClass || !dataManager || !imageInstances || classId < 0) {
            return false
        }

        let imageId = imageInstances.currentImageId !== undefined ? Number(imageInstances.currentImageId) : -1
        if (imageId < 0) {
            return false
        }

        let labelData = currentImageLabelData()
        let labelId = labelData && labelData.label_id !== undefined ? Number(labelData.label_id) : -1
        let currentClassId = labelData && labelData.label_class_id !== undefined ? Number(labelData.label_class_id) : -1

        if (labelId >= 0) {
            if (currentClassId !== classId) {
                dataManager.updateLabelsClass([labelId], [classId])
            }
            return true
        }

        return dataManager.addLabel(imageId, classId, {})
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
        let classId = labelClasses.data(modelIndex, LabelClassesModel.LabelClassIdRole)
        return classId !== undefined ? Number(classId) : -1
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

    function handleShortcutEvent(event) {
        if (!event || event.accepted || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            return false
        }
        return handleShortcutText(event.text)
    }

    function handleShortcutText(shortcut) {
        if (shortcutEditorOpen() || !labelClasses || !selection || !shortcut || shortcut.length === 0) {
            return false
        }

        let matchIndex = labelClasses.findByShortcut(shortcut)
        if (matchIndex < 0) {
            return false
        }
        return selectClassIndex(matchIndex, true)
    }

    function shortcutEditorOpen() {
        let editor = editorLoader.item
        return editor ? editor.visible : false
    }

    Component.onCompleted: {
        refreshViewModel()
        syncSelectionToCurrentImageClass()
    }
}
