from utils import symlink, get_file_path, dir_symlinks

if __name__ == '__main__':
    dir_symlinks('./build/bin/dltool', './build/dltool')
    # dir_symlinks('./build/bin/docs', './docs')
    all_dlls = get_file_path("./build/bin/dltool", ".dll")
    for dll in all_dlls:
        target_link = "./build/bin/" + dll.name
        symlink(dll, target_link)
        