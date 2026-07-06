import QtQuick
import QtQml.Models

import dltool.data
import dltool.ui
import quickui

Item {
    id: bridge
    visible: false

    property ImageLabelsListModel imageLabelsList: null
    property ItemSelectionModel selection: null

    Connections {
        target: SignalHelper
        function onImageLabelTableSelectionChanged(index, command) {
            if (bridge.selection) {
                bridge.selection.select(index, command)
            }
        }
        function onImageLabelTableShiftSelect(currentIndex, lastIndex, command) {
            if (bridge.imageLabelsList) {
                bridge.imageLabelsList.shiftSelect(currentIndex, lastIndex, command)
            }
        }
        function onImageLabelTableSelectAll() {
            if (bridge.imageLabelsList) {
                bridge.imageLabelsList.selectAll()
            }
        }
        function onImageLabelTableSelectionClear() {
            if (bridge.selection) {
                bridge.selection.clear()
            }
        }
    }
}
