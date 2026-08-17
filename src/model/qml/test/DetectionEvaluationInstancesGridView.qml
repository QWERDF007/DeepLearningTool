import dltool.model
import dltool.ui
import quickui

/*
 * 目标检测实例图像网格：显示置信度与 IoU。
 */
EvaluationInstancesGridViewBase {
    function formatMetric(model) {
        var score = Number(model.score)
        var scoreText = isFinite(score) ? score.toFixed(3) : "—"
        var iou = Number(model.iou)
        var iouText = isFinite(iou) ? Math.round(iou * 100) + "%" : "—"
        return scoreText + " / " + iouText
    }
}
