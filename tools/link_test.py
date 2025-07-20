import os
from utils import dir_symlinks

if __name__ == '__main__':
    print('tests dir exists: ', os.path.isdir('./build/tests'))
    if os.path.isdir('./build/tests'):
        dir_symlinks('./build/tests/dltool', './build/dltool')