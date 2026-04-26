import QtQuick 
import QtQuick.Controls
import QtQuick.Layouts 

import dltool.ui

HorizontalHeaderView {
    id: horizontalHeader
    clip: true
    columnSpacing: 0
    resizableColumns: false
    boundsBehavior: Flickable.StopAtBounds
    
    property int currentDimension: 0  // 0: instance, 1: image
    signal dimensionChanged(int dimension)
    
    model: ["类别名称", "分布比例", "数量"]

    property color borderColor: DltColor.Black
    
    delegate: Rectangle {
        implicitHeight: horizontalHeader.height
        color: DltColor.Highlight
        border.color: borderColor
        border.width: 1
        
        // 右侧分界线
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: borderColor
            visible: index < 2  // 最后一列不显示右侧分界线
        }

        // 第二列（分布比例）显示下拉列表
        Loader {
            anchors.fill: parent
            anchors.leftMargin: 15
            anchors.rightMargin: 15
            
            sourceComponent: {
                if (index === 0) {
                    return headerText
                }
                else if (index === 1) {
                    return dimensionComboBox
                } else {
                    return statsText
                }
            }
        }
        
        // 普通表头文本
        Component {
            id: headerText
            DltText {
                text: modelData
                font: DltFont.Subtitle
                verticalAlignment: Text.AlignVCenter
            }
        }

        Component {
            id: statsText
            RowLayout {
                anchors.fill: parent
                spacing: 0
                Item { // 添加一个容器来避免文本内容的实际渲染宽度不同，可能会导致视觉上的不对齐
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    DltText {
                        anchors.fill: parent
                        font: DltFont.Subtitle
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: "实例"
                    }
                }
                Item {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    DltText {
                        anchors.fill: parent
                        font: DltFont.Subtitle
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: "图像"
                    }
                }
            }
        }
        
        // 维度选择下拉列表
        Component {
            id: dimensionComboBox
            RowLayout {
                spacing: 10
                
                DltText {
                    text: modelData
                    font: DltFont.Subtitle
                    verticalAlignment: Text.AlignVCenter
                }
                
                Item {
                    Layout.fillWidth: true
                }
                
                DltComboBox {
                    Layout.preferredWidth: 160
                    model: ["按标签实例统计", "按图像统计"]
                    currentIndex: horizontalHeader.currentDimension
                    onCurrentIndexChanged: {
                        if (currentIndex !== horizontalHeader.currentDimension) {
                            horizontalHeader.currentDimension = currentIndex
                            horizontalHeader.dimensionChanged(currentIndex)
                        }
                    }
                }
            }
        }
    }
}
