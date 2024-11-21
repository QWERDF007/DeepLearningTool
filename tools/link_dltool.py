
from pathlib import Path
from utils import symlink, get_file_path
import platform

def sub_dir_symlinks(link_dir, target_dir):
    link_dir = Path(link_dir)
    if not link_dir.exists():
        link_dir.mkdir(parents=True)
    target_dir = Path(target_dir)
    subdirs_to_link = [subdir.stem for subdir in target_dir.iterdir() if subdir.is_dir()]

    for stem in subdirs_to_link:
        link = link_dir / stem
        target = target_dir / stem
        symlink(target, link)

def dir_symlinks(link_dir, target_dir):
    link_dir = Path(link_dir)
    target_dir = Path(target_dir)
    symlink(target_dir, link_dir)

if __name__ == '__main__':
    dir_symlinks('./build/bin/dltool', './build/dltool')
    # dir_symlinks('./build/bin/docs', './docs')
    all_dlls = get_file_path("./build/bin/dltool", ".dll")
    for dll in all_dlls:
        target_link = "./build/bin/" + dll.name
        symlink(dll, target_link)
        