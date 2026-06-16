set(OpenCV_HOME "D:/Software/dev/opencv-4.8.0/build/x64/vc16" CACHE PATH "OpenCV installation root" FORCE)
set(OpenCV_DIR "${OpenCV_HOME}/lib" CACHE PATH "OpenCV CMake package directory" FORCE) # dir contain .cmake
set(OpenCV_LIBRARY_DIR "${OpenCV_DIR}" CACHE PATH "OpenCV library directory" FORCE)
set(OpenCV_BIN_DIR "${OpenCV_HOME}/bin" CACHE PATH "OpenCV runtime directory" FORCE)
find_package(OpenCV REQUIRED) 
