import os

from qtpy.QtWidgets import QAction, QMenu
from qtpy.QtGui import QIcon, QPixmap
from qtpy.QtCore import QThread


here = os.path.dirname(os.path.abspath(__file__))

def newPixmap(pm:str):
    pixmap_dir = os.path.join(here, "../icons")
    return  QPixmap(os.path.join(":/", pixmap_dir, "%s.png" % pm))

def newIcon(icon:str):
    icons_dir = os.path.join(here, "../icons")
    return QIcon(os.path.join(":/", icons_dir, "%s.png" % icon))

def newAction(
    parent,
    text,
    slot=None,
    shortcut=None,
    icon=None,
    tip=None,
    checkable=False,
    enabled=True,
    checked=False,
):
    """Create a new action and assign callbacks, shortcuts, etc."""
    a = QAction(text, parent)
    if icon is not None:
        a.setIconText(text.replace(" ", "\n"))
        a.setIcon(newIcon(icon))
    if shortcut is not None:
        if isinstance(shortcut, (list, tuple)):
            a.setShortcuts(shortcut)
        else:
            a.setShortcut(shortcut)
    if tip is not None:
        a.setToolTip(tip)
        a.setStatusTip(tip)
    if slot is not None:
        a.triggered.connect(slot)
    if checkable:
        a.setCheckable(True)
    a.setEnabled(enabled)
    a.setChecked(checked)
    return a


def addActions(widget, actions):
    for action in actions:
        if action is None:
            widget.addSeparator()
        elif isinstance(action, QMenu):
            widget.addMenu(action)
        else:
            widget.addAction(action)


class struct(object):
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


class ImageReadThread(QThread):
    # read signal
    def __init__(self, parent=None):
        super().__init__(parent=parent)
        self._run_flag = True

    def run(self):
        # TODO: wait for read image, emit signal after read one image and read finished
        pass

    def stop(self):
        """Sets run flag to False and waits for thread to finish"""
        self._run_flag = False
        self.wait()