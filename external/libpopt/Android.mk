LOCAL_PATH:= $(call my-dir)
#include $(LOCAL_PATH)/../common.mk

# Build libpopt on target
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= $(call all-c-files-under,.)
LOCAL_C_INCLUDES := $(common_target_c_includes)
LOCAL_CFLAGS := $(common_target_cflags)
LOCAL_MODULE := libpopt

include $(BUILD_SHARED_LIBRARY)
