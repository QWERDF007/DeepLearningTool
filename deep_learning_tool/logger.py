import datetime
import logging
import termcolor

COLORS = {
    "WARNING": "yellow",
    "INFO": "white",
    "DEBUG": "blue",
    "CRITICAL": "red",
    "ERROR": "red",
}

from . import __appname__


class ColoredFormatter(logging.Formatter):
    def __init__(self, fmt, use_color=True):
        logging.Formatter.__init__(self, fmt)
        self.use_color = use_color

    def format(self, record):
        levelname = record.levelname
        if self.use_color and levelname in COLORS:

            def colored(text):
                return termcolor.colored(
                    text,
                    color=COLORS[levelname],
                    attrs={"bold": True},
                )
            record.levelname2 = colored("{:<7}".format(record.levelname))
            record.message2 = colored(record.msg)

            asctime2 = datetime.datetime.fromtimestamp(record.created)
            record.asctime2 = termcolor.colored(asctime2, color="green")
            record.pathname2 = record.pathname
            record.module2 = record.module
            record.funcName2 = record.funcName
            record.lineno2 = record.lineno
        return logging.Formatter.format(self, record)


class ColoredLogger(logging.Logger):

    FORMAT = "[%(asctime2)s] [%(levelname2)s] [%(pathname2)s, line %(lineno2)s, %(funcName2)s] %(message2)s"

    def __init__(self, name):
        logging.Logger.__init__(self, name, logging.DEBUG)

        color_formatter = ColoredFormatter(self.FORMAT)

        console = logging.StreamHandler()
        console.setFormatter(color_formatter)

        self.addHandler(console)
        return

LOGGER =  ColoredLogger(__appname__)
LOGGER.setLevel(logging.DEBUG)

