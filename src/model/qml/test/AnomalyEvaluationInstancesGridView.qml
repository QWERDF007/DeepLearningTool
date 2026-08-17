import dltool.model
import dltool.ui
import quickui

/*
 * 异常检测实例图像网格：仅显示图像分数。
 */
EvaluationInstancesGridViewBase {
    function formatMetric(model) {
        var score = Number(model.score)
        return isFinite(score) ? score.toFixed(3) : "—"
    }
}
