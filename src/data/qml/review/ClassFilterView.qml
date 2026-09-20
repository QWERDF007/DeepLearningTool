import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui
import "../component"

SidebarPanelBase {
    id: control
    title: "类别筛选"
    actionText: "清除筛选"
    actionIcon: QuiFontIcon.Clear
    onActionClicked: {
        if (!control.dataManager || !control.dataManager.globalFilter) {
            return
        }
        control.dataManager.globalFilter.clearFilter(GlobalFilter.FilterType.LabelClass)
        control.dataManager.globalFilter.setFilterEnabled(GlobalFilter.FilterType.LabelClass, false)
        control.activeClassId = -1
    }

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

    Connections {
        target: control.dataManager ? control.dataManager.globalFilter : null
        function onFilterStateChanged() {
            control.syncActiveFromGlobalFilter()
        }
    }

    ListView {
        id: view
        anchors.fill: parent
        clip: true
        spacing: 5
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: QuiScrollBar {}
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
