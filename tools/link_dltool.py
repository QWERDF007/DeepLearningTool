from utils import symlink, get_file_path, dir_symlinks
import platform

if __name__ == '__main__':
    dir_symlinks('./build/bin/dltool', './build/dltool')
    # dir_symlinks('./build/bin/docs', './docs')
    if platform.system() == "Windows":
        all_dlls = get_file_path("./build/bin/dltool", ".dll")
    elif platform.system() == "Linux":
        all_dlls = get_file_path("./build/bin/dltool", ".so")
    else:
        raise Exception("Unsupported platform: " + platform.system())
    for dll in all_dlls:
        target_link = "./build/bin/" + dll.name
        symlink(dll, target_link)
        