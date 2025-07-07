import QtQuick
import QtQuick.Controls

import dltool.ui

TextArea {
    id:control
    renderType: TextEdit.NativeRendering
    font: DltFont.Body
    color: DltColor.FontPrimary
    selectionColor: DltColor.Highlight
}
