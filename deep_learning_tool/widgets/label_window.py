from qtpy.QtCore import Qt

from qtpy.QtWidgets import QMainWindow, QWidget, QDockWidget, QListWidget
from qtpy.QtWidgets import QSizePolicy
from qtpy.QtWidgets import QGraphicsScene
from qtpy.QtWidgets import QGridLayout

from qtpy.QtGui import QPixmap

from deep_learning_tool import LOGGER
from deep_learning_tool.data import Project

from .label_graphics import ImageView
from .widget import ProjectInfo, HLine




class LabelWindow(QMainWindow):
    def __init__(self, parent: QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowFlags()) -> None:
        super().__init__(parent, flags)
        self.init()

    def init(self):
        LOGGER.debug("init")
        self.createCentralWidget()
        self.createDockWidgets()

        self.image_view.enable_cross_line(True)
        
        self.label_image = None
        

    def initLabelImage(self):
        LOGGER.debug("initLabelImage")
        if self.label_image is None and len(self.project.images_path) >= 1:
            image_path, index = next(iter(self.project.images_path.items()))
            self.label_image = self.scene.addPixmap(QPixmap(image_path))
            self.image_view.setLabelImage(self.label_image)


    def setLabelImage(self, image_path : str):
        LOGGER.debug("setLabelImage")
        if self.label_image is not None:
            self.label_image.setPixmap(QPixmap(image_path))
        else:
            self.label_image = self.scene.addPixmap(QPixmap(image_path))
        self.image_view.setLabelImage(self.label_image)
        

    def createCentralWidget(self):
        LOGGER.debug("createCentralWidget")
        label_widget = QWidget(self)
        label_widget_layout = QGridLayout()
        label_widget_layout.setContentsMargins(0,0,0,0)
        label_widget.setLayout(label_widget_layout)
        self.scene = QGraphicsScene(self)
        self.image_view = ImageView(self.scene, label_widget)
        self.image_view.show()
        label_widget_layout.addWidget(self.image_view)
        self.setCentralWidget(label_widget)


    def setProject(self, project : Project):
        self.project = project
        self.project_dock.setWidget(ProjectInfo(project.name, project.type))

    
    def createDockWidgets(self):
        LOGGER.debug("createDockWidgets")
        empty_widget = QWidget(self)
        # line = HLine(self)

        self.project_dock = QDockWidget(self.tr("项目"), self)
        self.project_dock.setTitleBarWidget(empty_widget)
        self.project_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.project_dock.setContentsMargins(0,0,0,0)
        self.project_dock.setFixedHeight(100)
        self.project_dock.setMinimumWidth(100)
        self.project_dock.setMaximumWidth(500)
        self.project_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.project_dock)


        self.file_dock = QDockWidget(self.tr("图像"), self)
        # self.file_dock.setTitleBarWidget(line)
        self.file_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.file_dock.setWidget(QWidget(self))
        self.file_dock.setFixedHeight(120)
        self.file_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.file_dock)


        self.label_list_dock = QDockWidget(self.tr("标签列表"), self)
        self.label_list_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.label_list_dock.setWidget(QListWidget(self))
        self.label_list_dock.setMinimumHeight(100)
        self.label_list_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.label_list_dock)


        self.split_mapping_dock = QDockWidget(self.tr("拆分映射"), self)
        self.split_mapping_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.split_mapping_dock.setWidget(QWidget(self))
        self.split_mapping_dock.setMinimumHeight(100)
        self.split_mapping_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.split_mapping_dock)


        self.navigator_dock = QDockWidget(self.tr("导航器"), self)
        self.navigator_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.navigator_dock.setWidget(QWidget(self))
        self.navigator_dock.setFixedHeight(100)
        self.navigator_dock.setMinimumWidth(100)
        self.navigator_dock.setMaximumWidth(500)
        self.navigator_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.navigator_dock)


        self.image_enhance_dock = QDockWidget(self.tr("图像增强"), self)
        self.image_enhance_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.image_enhance_dock.setWidget(QWidget(self))
        self.image_enhance_dock.setFixedHeight(100)
        self.image_enhance_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.image_enhance_dock)


        self.shape_dock = QDockWidget(self.tr("标签实例"), self)
        self.shape_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.shape_dock.setWidget(QListWidget(self))
        self.shape_dock.setMinimumHeight(100)
        self.shape_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.shape_dock)
        

        self.shape_edit_dock = QDockWidget(self.tr("标签实例编辑"), self)
        self.shape_edit_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.shape_edit_dock.setWidget(QWidget(self))
        self.shape_edit_dock.setMinimumHeight(100)
        self.shape_edit_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.shape_edit_dock)

        
