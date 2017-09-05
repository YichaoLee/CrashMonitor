

cmake_minimum_required(VERSION 3.4.1)
#set (CMAKE_CXX_STANDARD 11)
aux_source_directory(src/main/cpp/libunwind DIR_LIB_SRCS)

add_library( crashMonitor
             SHARED
             src/main/cpp/crashMonitor.cpp src/main/cpp/NativeCrashHandler.h)

find_library( log-lib
              log )

#add_library( unwind SHARED IMPORTED)
#set_target_properties( unwind PROPERTIES IMPORTED_LOCATION ../../../../src/main/jniLibs/${ANDROID_ABI}/libunwind.so)

target_link_libraries( crashMonitor
                        #unwind
                        ${log-lib} )