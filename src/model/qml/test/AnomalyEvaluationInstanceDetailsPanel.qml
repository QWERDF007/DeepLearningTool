import dltool.model
import dltool.ui
import quickui

/*
 * 异常检测实例详情面板：只展示图像分数，预测类别颜色按正常/异常。
 */
EvaluationInstanceDetailsPanelBase {
    metricLabel: qsTr("分数")

    function formatMetric(instance) {
        var score = Number(instance.score)
        return isFinite(score) ? score.toFixed(3) : "—"
    }

    function predictedClassColor(instance) {
        if (!instance)
            return QuiColor.FontDark
        return Number(instance.predClassId) === 1 ? "red" : "green"
    }
}
