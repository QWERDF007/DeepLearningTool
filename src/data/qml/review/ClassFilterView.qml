import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Rectangle { 
    id: control
    color: QuiColor.Primary

    property DataManager dataManager
    property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null

    property int activeClassId: -1

    function syncActiveFromGlobalFilter() {
        if (!control.dataManager || !control.dataManager.globalFilter) {
            control.activeClassId = -1
            return
        }

        let ids = control.dataManager.globalFilter.getActiveIds(GlobalFilter.FilterType.LabelClass)
        control.activeClassId = (ids && ids.length > 0) ? ids[0] : -1
    }

    onDataManagerChanged: control.syncActiveFromGlobalFilter()

    Component.onCompleted: control.syncActiveFromGlobalFilter()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        
        ClassFilterHeader {
            id: header
            Layout.fillWidth: true
            height: 32
            dataManager: control.dataManager

            onClearClicked: {
                control.activeClassId = -1
            }
        }

        Connections {
            target: control.dataManager ? control.dataManager.globalFilter : null
            function onFilterStateChanged() {
                control.syncActiveFromGlobalFilter()
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
            model: control.labelClasses

            delegate: ClassFilterDelegate {
                width: view.width - 8
                height: 32
                backgroundColor: (control.activeClassId === model.label_class_id) ? QuiColor.Highlight : Qt.lighter(QuiColor.Primary, 1.2)
                className: model.name
                classColor: model.color
                classShortcut: model.shortcut
                classId: model.label_class_id
                ordinalIndex: model.ordinal_index
                listView: view
                labelClasses: control.labelClasses

                onClicked: function(classId) {
                    if (!control.dataManager || !control.dataManager.globalFilter) {
                        return
                    }

                    control.activeClassId = classId
                    control.dataManager.globalFilter.setFilter(GlobalFilter.FilterType.LabelClass, [classId])
                    control.dataManager.globalFilter.setFilterEnabled(GlobalFilter.FilterType.LabelClass, true)
                }
            }
        }
    }
}
