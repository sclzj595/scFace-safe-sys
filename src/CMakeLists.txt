# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# 收集源文件
file(GLOB SRCS
    "*.cpp"
    "*.h"
)

add_executable(FaceGate ${SRCS})

target_link_libraries(FaceGate
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::SerialPort
    ${OpenCV_LIBS}
    mvcore            # 套件共享基础库：FrameBank/PathResolver/EventId（header-only）
    FaceAlgorithm
)

set_target_properties(FaceGate PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 运行时 DLL 部署（OpenCV SDK 复用 SS 仓内同版本包，套件内相对引用）
if(WIN32)
  add_custom_command(TARGET FaceGate POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/../SmartSurveillance/utils/opencv4_5_2/x64/mingw/bin/"
      $<TARGET_FILE_DIR:FaceGate>
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/Qt5Core.dll"
      "${Qt5_ROOT}/bin/Qt5Gui.dll"
      "${Qt5_ROOT}/bin/Qt5Widgets.dll"
      "${Qt5_ROOT}/bin/Qt5Concurrent.dll"
      "${Qt5_ROOT}/bin/Qt5SerialPort.dll"
      $<TARGET_FILE_DIR:FaceGate>
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/libstdc++-6.dll"
      "${Qt5_ROOT}/bin/libgcc_s_seh-1.dll"
      "${Qt5_ROOT}/bin/libwinpthread-1.dll"
      $<TARGET_FILE_DIR:FaceGate>
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:FaceGate>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/plugins/platforms/qwindows.dll"
      "$<TARGET_FILE_DIR:FaceGate>/platforms"
    COMMENT "Deploying runtime DLLs to output directory"
  )
endif()
