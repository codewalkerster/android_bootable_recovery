LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(TARGET_ARCH), arm64)
  ifeq ($(TARGET_BOARD_PLATFORM), rk3399)
    LOCAL_PREBUILT_LIBS := lib64/vendor/librkupdate.a
  else ifeq ($(TARGET_BOARD_PLATFORM), rk3328)
    LOCAL_PREBUILT_LIBS := lib64/vendor/librkupdate.a
  else
    LOCAL_PREBUILT_LIBS := lib64/librkupdate.a
  endif
else ifeq ($(TARGET_ARCH), arm)
    LOCAL_PREBUILT_LIBS := lib/librkupdate.a
endif

include $(BUILD_MULTI_PREBUILT)
