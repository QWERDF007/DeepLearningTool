
from collections import OrderedDict

class Project(object):
    def __init__(self, project_name, project_type) -> None:
        self.images_path = OrderedDict()
        self.project_name = project_name
        self.project_type = project_type

    @property
    def name(self):
        return self.project_name
    
    @property
    def type(self):
        return self.project_type
    
    def addImage(self, img_path : str):
        self.images_path[img_path] = len(self.images_path) + 1
