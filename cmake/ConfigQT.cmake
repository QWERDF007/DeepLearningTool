# 设置 Qt 的 cmake 文件目录
set(Qt6_CMAKE_DIR  "E:/Softwares/Qt/6.7.2/msvc2019_64/lib/cmake")
set(Qt6_DIR  "${Qt6_CMAKE_DIR}/Qt6")
set(Qt6QmlTools_DIR  "${Qt6_CMAKE_DIR}/Qt6QmlTools")
set(QT_QML_IMPORT_DIR "E:/Softwares/Qt/6.7.2/msvc2019_64/qml")


# 寻找Qt6的组件
find_package(Qt6  REQUIRED COMPONENTS Core Gui Quick Widgets Charts)

# 寻找Qt6的测试组件
find_package(Qt6  REQUIRED COMPONENTS QuickTest)

# 设置Qt6项目
qt_standard_project_setup(REQUIRES 6)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


# 设置 QTP0001 策略为 NEW，即使使用新的资源前缀 :/qt/qml/ 作为默认值
qt_policy(SET QTP0001 NEW)

# 添加 qml 导入目录
# QML_IMPORT_PATH 用于支持语法高亮
list(APPEND QML_IMPORT_PATH ${CMAKE_BINARY_DIR})

list(APPEND QML_IMPORT_PATH ${QT_QML_IMPORT_DIR})

# 去除重复
list(REMOVE_DUPLICATES QML_IMPORT_PATH)

# 将导入目录添加到 cache, 强制更新
set(QML_IMPORT_PATH ${QML_IMPORT_PATH}
    CACHE STRING "qml import paths"
    FORCE
)
