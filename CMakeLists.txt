cmake_minimum_required(VERSION 3.9)

project(wii-u-gc-adapter LANGUAGES C)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(PC_LIBUSB libusb-1.0)
pkg_check_modules(PC_UDEV libudev)

add_executable(wii-u-gc-adapter wii-u-gc-adapter.c)
target_include_directories(wii-u-gc-adapter PRIVATE ${PC_LIBUSB_INCLUDE_DIRS} ${PC_UDEV_INCLUDE_DIRS})
target_link_libraries(wii-u-gc-adapter PRIVATE ${PC_LIBUSB_LIBRARIES} ${PC_UDEV_LIBRARIES} Threads::Threads)
target_compile_options(wii-u-gc-adapter PRIVATE ${PC_UDEV_CFLAGS} -Wall -Wextra -pedantic -Wno-format)
set_target_properties(wii-u-gc-adapter PROPERTIES C_STANDARD 99)
