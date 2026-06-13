import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: root
    color: QuiColor.Primary

    // 公共属性
    property DataManager dataManager
    property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    property LabelInstancesModel labelInstances: dataManager ? dataManager.labelInstances : null
    property ItemSelectionModel selection: labelInstances ? labelInstances.selection : null
    property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null

    // 内部状态属性
    property int selectedCount: 0
    property int totalCount: 0
    property string imageName: ""
    property string datasetName: ""
    property string tagName
    property int currentClassId: -1
    property bool multipleDifferentDatasets: false
    property bool multipleDifferentImages: false
    property bool multipleDifferentTags: false
    property bool multipleDifferentClasses: false

    // 可见性绑定：只有当有选中项时才显示
    visible: selectedCount > 0

    QuiMenu {
        id: menu
        width: 200
        QuiMenuItem {
            text: "复制"
            onTriggered: {
                copyboard.selectAll()
                copyboard.copy()
            }
        }
    }
    TextEdit {
        id: copyboard
        visible: false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5

        SelectedLabelsInfoHeader {
            Layout.fillWidth: true
            height: 32
            onClicked: root.clearSelection()
        }

        // 行1: 选中数量统计
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            QuiText {
                Layout.leftMargin: 5
                Layout.rightMargin: 5
                text: "选中:"
                textColor: QuiColor.FontDark
            }

            QuiText {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                Layout.rightMargin: 5
                text: root.selectedCount + "/" + root.totalCount
                
            }
        }

        InfoTextItem {
            title:  "数据集"
            text: root.multipleDifferentDatasets ? "不同数据集" : root.datasetName
            onClicked: {
                copyboard.text = text
                menu.popup()
            }
        }

        InfoTextItem {
            title:  "图像"
            text: root.multipleDifferentImages ? "不同图像" : root.imageName
            onClicked: {
                copyboard.text = text
                menu.popup()
            }
        }

        InfoTextItem {
            title:  "图像Tag"
            text: root.multipleDifferentImages ? "不同Tag" : root.tagName
            onClicked: {
                copyboard.text = text
                menu.popup()
            }
        }

        // 行4: 类别选择器
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            QuiText {
                Layout.leftMargin: 5
                Layout.rightMargin: 5
                text: "类别:"
                textColor: QuiColor.FontDark
            }

            LabelClassSelector {
                id: classSelector
                Layout.fillWidth: true
                Layout.leftMargin: 5
                Layout.rightMargin: 5
                Layout.preferredHeight: 32

                labelClassesModel: root.labelClasses
                currentClassId: root.currentClassId
                showMultipleDifferent: root.multipleDifferentClasses
                multipleDifferentText: "不同类别"

                onClassChanged: function(newClassId) {
                    root.changeSelectedLabelsClass(newClassId)
                }
            }
        }
        // 填充剩余空间
        Item {
            Layout.fillHeight: true
        }
    }

    // 核心方法

    // 更新选中信息
    function updateSelectionInfo() {
        if (!selection || !labelInstances) {
            selectedCount = 0
            return
        }

        // 获取选中的索引列表
        let selectedIndexes = selection.selectedIndexes
        selectedCount = selectedIndexes.length
        totalCount = labelInstances.rowCount()

        if (selectedCount === 0) {
            return
        }

        // 收集图像ID和类别ID
        let imageIds = new Set()
        let classIds = new Set()

        for (let i = 0; i < selectedIndexes.length; i++) {
            let index = selectedIndexes[i]
            let imageId = labelInstances.data(index, LabelInstancesModel.ImageIdRole)
            let classId = labelInstances.data(index, LabelInstancesModel.LabelClassIdRole)

            imageIds.add(imageId)
            classIds.add(classId)
        }

        // 更新图像信息
        if (imageIds.size === 1) {
            multipleDifferentImages = false
            let imageId = Array.from(imageIds)[0]
            imageName = dataManager.getImageName(imageId)
            datasetName = dataManager.getImageDatasetName(imageId)
            tagName = dataManager.getImageTagName(imageId)
        } else {
            multipleDifferentImages = true
            imageName = ""
            datasetName = ""
        }

        // 更新类别信息
        if (classIds.size === 1) {
            multipleDifferentClasses = false
            currentClassId = Array.from(classIds)[0]
        } else {
            multipleDifferentClasses = true
            currentClassId = -1
        }
    }
    
    // 清除选中状态
    function clearSelection() {
        if (selection) {
            selection.clear()
        }
    }

    // 修改选中标注的类别
    function changeSelectedLabelsClass(newClassId) {
        if (!selection || !labelInstances || !dataManager) {
            return
        }

        // 获取选中的标注ID列表
        let selectedIndexes = selection.selectedIndexes
        let labelIds = []

        for (let i = 0; i < selectedIndexes.length; i++) {
            let index = selectedIndexes[i]
            let labelId = labelInstances.data(index, LabelInstancesModel.LabelIdRole)
            labelIds.push(labelId)
        }

        if (labelIds.length === 0) {
            return
        }

        // 创建类别ID数组（所有标注都改为同一类别）
        let classIds = new Array(labelIds.length).fill(newClassId)

        // 调用 DataManager 的批量更新方法
        dataManager.updateLabelsClass(labelIds, classIds)
    }

    // 信号连接

    Connections {
        target: selection
        function onSelectionChanged() {
            root.updateSelectionInfo()
        }
    }

    Connections {
        target: labelInstances
        function onRowsInserted() {
            root.updateSelectionInfo()
        }
        function onRowsRemoved() {
            root.updateSelectionInfo()
        }
        function onDataChanged() {
            root.updateSelectionInfo()
        }
    }

    // 组件完成时初始化
    Component.onCompleted: {
        updateSelectionInfo()
    }
}
