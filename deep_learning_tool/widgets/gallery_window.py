from qtpy.QtCore import Qt, Signal, Slot
from qtpy.QtCore import QDir, QDirIterator

from qtpy.QtWidgets import QMainWindow, QDockWidget, QWidget
from qtpy.QtWidgets import QFileDialog, QSizePolicy
from qtpy.QtWidgets import QGridLayout

from deep_learning_tool.data import Project
from deep_learning_tool.configs import image_suffixs_filter
from deep_learning_tool import LOGGER

from .widget import ProjectInfo
from .gallery_widget import GalleryImageInfo
from .gallery_graphics import GalleryItem, GalleryView





class GalleryWindow(QMainWindow):
    
    galleryItemDoubleClicked = Signal(str)

    def __init__(self, parent: QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowFlags()) -> None:
        super().__init__(parent, flags)
        self.init()

    def init(self):
        self.createCentralWidget()
        self.createDockWidgets()

        self.scene = self.gallery_view.scene()
        self.gallery_view.show()
        self.gallery_view.galleryItemDoubleClicked.connect(self._galleryItemDoubleClicked)

        self.initSignals()


    def initSignals(self):
        self.gallery_image_info.add_folder_btn.clicked.connect(self.openFolder)
        self.gallery_image_info.add_image_btn.clicked.connect(self.openImage)

    @Slot()
    def openImage(self):
        file_path, _ = QFileDialog().getOpenFileName(self, "选择图片加入项目", "/home",  image_suffixs_filter)
        if file_path != "" and self.project.images_path.get(file_path) is None:
            self.project.addImage(file_path)
            self.gallery_view.addImage(GalleryItem(file_path))
            self.gallery_view.update()

    @Slot()
    def openFolder(self):
        filter = ["*.png", "*.jpg", "*.bmp"]
        folder = QFileDialog().getExistingDirectory(self, 
                                                    "选择目录加入项目", 
                                                    "/home", 
                                                    QFileDialog.Option.ShowDirsOnly | QFileDialog.Option.DontResolveSymlinks)
        if folder != "":
            LOGGER.debug(f"folder: {folder}")
            for fname in QDirIterator(folder, 
                                      filter, 
                                      QDir.Filter.NoDotAndDotDot | QDir.Filter.Files | QDir.Filter.NoSymLinks, 
                                      QDirIterator.IteratorFlag.Subdirectories):
                LOGGER.debug(fname)

    @Slot(str)
    def _galleryItemDoubleClicked(self, image_path : str) -> None:
        self.galleryItemDoubleClicked.emit(image_path)


    def createCentralWidget(self):
        gallery_widget = QWidget(self)
        gallery_widget_layout = QGridLayout()
        gallery_widget_layout.setContentsMargins(0,0,0,0)
        gallery_widget.setLayout(gallery_widget_layout)
        self.gallery_view = GalleryView(gallery_widget)
        gallery_widget_layout.addWidget(self.gallery_view)
        self.setCentralWidget(gallery_widget)

    def setProject(self, project : Project):
        self.project = project
        self.project_dock.setWidget(ProjectInfo(project.name, project.type))


    def createDockWidgets(self):
        empty_widget = QWidget(self)
        # line = HLine(self)
        # line.setLineWidth(2)

        self.project_dock = QDockWidget(self.tr("项目"), self)
        self.project_dock.setTitleBarWidget(empty_widget)
        self.project_dock.setFeatures(QDockWidget.DockWidgetMovable)
        self.project_dock.setWidget(ProjectInfo("示例项目", "目标检测"))
        self.project_dock.setContentsMargins(0,0,0,0)
        self.project_dock.setMinimumWidth(120)
        self.project_dock.setMaximumWidth(500)
        self.project_dock.setFixedHeight(100)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.project_dock)


        self.file_dock = QDockWidget(self.tr(""), self)
        # self.file_dock.setTitleBarWidget(line)
        self.file_dock.setFeatures(QDockWidget.DockWidgetMovable)
        self.gallery_image_info = GalleryImageInfo(self)
        self.gallery_image_info.setMinimumHeight(40)
        self.gallery_image_info.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.file_dock.setWidget(self.gallery_image_info)
        self.file_dock.setMinimumHeight(40)
        self.file_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.file_dock)

        # self.file_dock = QDockWidget(self.tr("图像"), self)
        # self.select_dock = QDockWidget(self.tr("已选择"), self)
        self.split_mapping_dock = QDockWidget(self.tr("拆分映射"), self)
        self.split_mapping_dock.setFeatures(QDockWidget.NoDockWidgetFeatures)
        widget = QWidget(self)
        widget.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Expanding)
        self.split_mapping_dock.setWidget(widget)
        self.split_mapping_dock.setMinimumHeight(100)
        self.split_mapping_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Expanding)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.split_mapping_dock)

        
        