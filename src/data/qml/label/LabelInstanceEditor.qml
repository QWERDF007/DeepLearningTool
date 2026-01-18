import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Rectangle {
    id: control
    color: DltColor.Primary

    // 公共属性
    property DataManager dataManager
    property ImageLabelsListModel imageLabelsList: dataManager ? dataManager.imageLabelsList : null
    property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null
    property ItemSelectionModel selection: imageLabelsList ? imageLabelsList.selection : null

    // 内部状态
    property var selectedLabelData: null
    property int selectedLabelId: -1
    property int selectedLabelClassId: -1
    property bool hasSelection: false
    property bool multiSelection: false
    property bool _updating: false

    // 当选择改变时更新选中的标注数据
    Connections {
        target: selection
        function onSelectionChanged() {
            control.updateSelectedLabel()
        }
    }

    // 当 imageLabelsList 数据改变时也更新
    Connections {
        target: imageLabelsList
        function onDataChanged() {
            if (control.hasSelection && !control.multiSelection) {
                control.updateSelectedLabel()
            }
        }
    }

    // 更新选中的标注数据
    function updateSelectedLabel() {
        if (!selection || !imageLabelsList) {
            hasSelection = false
            multiSelection = false
            selectedLabelData = null
            selectedLabelId = -1
            selectedLabelClassId = -1
            return
        }

        let selectedIndexes = selection.selectedIndexes
        if (selectedIndexes.length === 0) {
            hasSelection = false
            multiSelection = false
            selectedLabelData = null
            selectedLabelId = -1
            selectedLabelClassId = -1
        } else if (selectedIndexes.length === 1) {
            hasSelection = true
            multiSelection = false
            let index = selectedIndexes[0].row
            selectedLabelData = imageLabelsList.getData(index)
            selectedLabelId = selectedLabelData.label_id
            selectedLabelClassId = selectedLabelData.label_class_id
        } else {
            hasSelection = true
            multiSelection = true
            selectedLabelData = null
            selectedLabelId = -1
            selectedLabelClassId = -1
        }
    }

    // 提交类别修改
    function commitClassChange(newClassId) {
        if (_updating || !dataManager || selectedLabelId === -1) return
        _updating = true
        dataManager.updateLabelsClass([selectedLabelId], [newClassId])
        // 不在这里手动更新 selectedLabelClassId
        // 当 dataChanged 信号发出时会自动更新
        _updating = false
    }

    // 提交几何属性修改
    function commitGeometryChange(x, y, w, h) {
        if (_updating || !dataManager || selectedLabelId === -1 || !selectedLabelData) return
        _updating = true
        let data = {
            x: x,
            y: y,
            width: w,
            height: h
        }
        dataManager.updateLabels([selectedLabelId], [data])
        _updating = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12

        // 标题
        DltText {
            text: "编辑实例"
            font: DltFont.Subtitle
            Layout.fillWidth: true
        }

        // 内容区域
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: {
                if (!control.hasSelection) {
                    return emptyStateComponent
                } else if (control.multiSelection) {
                    return multiSelectStateComponent
                } else {
                    return editorComponent
                }
            }
        }
    }

    // 空状态组件
    Component {
        id: emptyStateComponent
        Item {
            DltText {
                anchors.centerIn: parent
                text: "请选择一个标注实例"
                color: DltColor.FontDark
            }
        }
    }

    // 多选状态组件
    Component {
        id: multiSelectStateComponent
        Item {
            DltText {
                anchors.centerIn: parent
                text: "已选择多个实例，不支持批量编辑"
                color: DltColor.FontDark
            }
        }
    }

    // 编辑器组件
    Component {
        id: editorComponent
        ColumnLayout {
            spacing: 8

            // 类别选择器行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                DltText {
                    text: "类别"
                    Layout.preferredWidth: 60
                    Layout.alignment: Qt.AlignVCenter
                }

                LabelClassSelector {
                    id: classSelector
                    Layout.fillWidth: true
                    labelClassesModel: control.labelClasses
                    currentClassId: control.selectedLabelClassId
                    enabled: control.hasSelection && !control.multiSelection

                    onClassChanged: function(newClassId) {
                        control.commitClassChange(newClassId)
                    }
                }
            }

            // X 行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                DltText {
                    text: "X"
                    Layout.preferredWidth: 60
                    Layout.alignment: Qt.AlignVCenter
                }

                DltSpinEditor {
                    id: xEditor
                    Layout.fillWidth: true
                    label: ""
                    value: control.selectedLabelData ? control.selectedLabelData.x : 0
                    minValue: 0
                    maxValue: 10000
                    step: 1
                    decimals: 0
                    enabled: control.hasSelection && !control.multiSelection

                    onEditingFinished: {
                        control.commitGeometryChange(xEditor.value, yEditor.value, widthEditor.value, heightEditor.value)
                    }
                }
            }

            // Y 行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                DltText {
                    text: "Y"
                    Layout.preferredWidth: 60
                    Layout.alignment: Qt.AlignVCenter
                }

                DltSpinEditor {
                    id: yEditor
                    Layout.fillWidth: true
                    label: ""
                    value: control.selectedLabelData ? control.selectedLabelData.y : 0
                    minValue: 0
                    maxValue: 10000
                    step: 1
                    decimals: 0
                    enabled: control.hasSelection && !control.multiSelection

                    onEditingFinished: {
                        control.commitGeometryChange(xEditor.value, yEditor.value, widthEditor.value, heightEditor.value)
                    }
                }
            }

            // 宽度行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                DltText {
                    text: "宽度"
                    Layout.preferredWidth: 60
                    Layout.alignment: Qt.AlignVCenter
                }

                DltSpinEditor {
                    id: widthEditor
                    Layout.fillWidth: true
                    label: ""
                    value: control.selectedLabelData ? control.selectedLabelData.width : 0
                    minValue: 1
                    maxValue: 10000
                    step: 1
                    decimals: 0
                    enabled: control.hasSelection && !control.multiSelection

                    onEditingFinished: {
                        control.commitGeometryChange(xEditor.value, yEditor.value, widthEditor.value, heightEditor.value)
                    }
                }
            }

            // 高度行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                DltText {
                    text: "高度"
                    Layout.preferredWidth: 60
                    Layout.alignment: Qt.AlignVCenter
                }

                DltSpinEditor {
                    id: heightEditor
                    Layout.fillWidth: true
                    label: ""
                    value: control.selectedLabelData ? control.selectedLabelData.height : 0
                    minValue: 1
                    maxValue: 10000
                    step: 1
                    decimals: 0
                    enabled: control.hasSelection && !control.multiSelection

                    onEditingFinished: {
                        control.commitGeometryChange(xEditor.value, yEditor.value, widthEditor.value, heightEditor.value)
                    }
                }
            }

            // 填充剩余空间
            Item {
                Layout.fillHeight: true
            }
        }
    }
}
