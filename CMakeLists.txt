# CMakeList.txt: getIpv4Address 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.8)

set(CMAKE_CXX_STANDARD 17)  # 设置C++标准为C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # 确保编译器遵循C++17标准
set(CMAKE_CXX_EXTENSIONS OFF)  # 不使用编译器特定的扩展

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("getIpv4Address")

# 将源代码添加到此项目的可执行文件。
add_executable (getIpv4Address "getIpv4Address.cpp" "getIpv4Address.h")

find_library(LPHELP_LIBRARY iphlpapi)
find_library(WS32_LIBRARY Ws2_32)
 
if(NOT LPHELP_LIBRARY)
    message(FATAL_ERROR "Cannot find iphlpapi.lib")
endif()

if(NOT WS32_LIBRARY)
    message(FATAL_ERROR "Cannot find Ws2_32.lib")
endif()
 
target_link_libraries(getIpv4Address ${LPHELP_LIBRARY})
target_link_libraries(getIpv4Address ${WS32_LIBRARY})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET getIpv4Address PROPERTY CXX_STANDARD 20)
endif()

# TODO: 如有需要，请添加测试并安装目标。
