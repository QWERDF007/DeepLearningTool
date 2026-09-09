
# 将C++标准设置为20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# 在RelWithDebInfo模式下给CXX编译器添加-O3和-ggdb参数
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 -ggdb")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -O3 -ggdb")

# 使用 CMake 3.20 之前的旧行为处理 Ninja 的 DEPFILES 生成器，这影响依赖文件的生成方式。
# cmake_policy(SET CMP0116 OLD)

if(WARNINGS_AS_ERRORS)
    # 设置C语言警告为错误
    set(C_WARNING_ERROR_FLAG "-Werror")
    # 设置CUDA语言警告全部为错误
    set(CUDA_WARNING_ERROR_FLAG "-Werror all-warnings")
endif()

# -Wall：启用所有警告
# -Wno-unknown-pragmas：忽略未知的 pragma 指令
# -Wpointer-arith：对指针算术运算发出警告
# -Wmissing-declarations：对缺少声明的函数或变量发出警告
# -Wredundant-decls：对冗余的声明发出警告
# -Wmultichar：对多字符字符常量发出警告
# -Wno-unused-local-typedefs：禁止对未使用的局部类型定义发出警告
# -Wunused：对未使用的变量、函数或标签发出警告
# Match warning setup with GVS
if (MSVC)
    # set(C_WARNING_FLAGS "-Wall")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8 /bigobj")
    set(C_WARNING_FLAGS "-W4")
    # set(CXX_WARNING_FLAGS "/permissive-")
else ()
    set(C_WARNING_FLAGS "-Wall -Wno-unknown-pragmas -Wpointer-arith -Wmissing-declarations -Wredundant-decls -Wmultichar -Wno-unused-local-typedefs -Wunused")
    # 派生类中的虚函数声明中建议使用 override 关键字
    set(CXX_WARNING_FLAGS "-Wsuggest-override")
    # 禁止编译器在比较两个常量时发出警告
    set(CUDA_WARNING_FLAGS "-Wno-tautological-compare")
endif ()

# 设置 C++ 和 C 编译标志
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${C_WARNING_ERROR_FLAG} ${C_WARNING_FLAGS} ${CXX_WARNING_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_WARNING_ERROR_FLAG} ${C_WARNING_FLAGS}")
# 设置 CUDA 编译标志
if (MSVC)
    add_definitions(-DNOMINMAX)
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} ${CUDA_WARNING_ERROR_FLAG} ${CUDA_WARNING_FLAGS}")
else ()
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} ${CUDA_WARNING_ERROR_FLAG} ${C_WARNING_FLAGS} ${CXX_WARNING_FLAGS} ${CUDA_WARNING_FLAGS}")
endif ()

# 如果使用 GCC, 确保版本不低于 GCC 9.4, 否则给出错误并终止配置
# if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 9.4)
#     message(FATAL_ERROR "Must use gcc>=9.4 to compile CV-CUDA, you're using ${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
# endif()

# 包含 CheckIPOSupported 模块
include(CheckIPOSupported)
# 检查当前编译器是否支持链接时间优化(LTO)
check_ipo_supported(RESULT LTO_SUPPORTED)
# 默认启用
set(LTO_ENABLED ON)


# 开启 sanitizer 来检测代码问题 (支持 ENABLE_SANITIZER 与 DLT_ENABLE_SANITIZER)
if(ENABLE_SANITIZER OR DLT_ENABLE_SANITIZER)
    set(ENABLE_SANITIZER ON CACHE BOOL "Enabled sanitized build" FORCE)
    set(DLT_ENABLE_SANITIZER ON CACHE BOOL "Enabled sanitized build" FORCE)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_sanitizer_compile_flags
            "-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=leak -fsanitize=undefined -fno-sanitize-recover=all")
        set(_sanitizer_link_flags "-fsanitize=address -fsanitize=undefined")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_sanitizer_compile_flags}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_sanitizer_compile_flags}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_sanitizer_link_flags}")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_sanitizer_link_flags}")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_sanitizer_compile_flags
            "-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=undefined -fno-sanitize-recover=all")
        set(_sanitizer_link_flags "-fsanitize=address -fsanitize=undefined")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_sanitizer_compile_flags}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_sanitizer_compile_flags}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_sanitizer_link_flags}")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_sanitizer_link_flags}")
    elseif(MSVC)
        set(_sanitizer_compile_flags "/fsanitize=address")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_sanitizer_compile_flags}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_sanitizer_compile_flags}")
    endif()
endif()

