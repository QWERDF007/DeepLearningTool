import sys

from qtpy import QtWidgets
from .app import MainWindow
from deep_learning_tool import __appname__, __version__, LOGGER


def main():
    LOGGER.info(f"welcome to {__appname__}")
    app = QtWidgets.QApplication(sys.argv)
    app.setApplicationName(__appname__)
    app.setApplicationVersion(__version__)
    win = MainWindow()

    win.show()
    win.raise_()
    status = app.exec_()
    LOGGER.info("bye")
    sys.exit(status)

if __name__ == '__main__':
    main()
