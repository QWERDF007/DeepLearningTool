import os
import re
import sys
import ctypes
from pathlib import Path
from typing import Union


# def get_file_path(path, suffix) -> list:
#     """ 递归遍历目录获取指定后缀的文件路径

#     Args:
#         path (str): 待遍历的目录
#         suffix (str): 文件后缀

#     Returns:
#         [str]: 具有指定后缀的文件路径列表
#     """
#     file_list = []
#     for root, dirs, files in os.walk(path):
#         for file in files:
#             if file.endswith(suffix):
#                 file_list.append(os.path.join(root, file))  
#     return file_list

def get_file_path(path, suffix) -> list:
    return [file for file in Path(path).rglob("*"+suffix)]

def is_admin() -> bool:
    """判断是否管理员权限

    Returns:
        bool: True/False
    """
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        return False
    
def runas_admin():
    """_summary_
    """
    if not is_admin():
        ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, __file__, None, 1)
        
def symlink(target : Union[str, Path], link : Union[str, Path]):
    if isinstance(target, str):
        target = Path(target)
    if isinstance(link, str):
        link = Path(link)
    target = target.absolute()
    link = link.absolute()
    if link.exists():
        link.unlink()
    if not target.exists():
        print(f'failed to create symlink, not found {target}')
        return
    print(f'create symlink {link} to {target}')
    link.symlink_to(target)

def find_pattern_in_cmake(cmake_config_file, pattern):
    result = None
    with open(cmake_config_file, 'r', encoding='utf-8') as f:
        for line in f:
            group = re.match(pattern, line)
            if group:
                result = Path(group.group(1))
                break
    return result
