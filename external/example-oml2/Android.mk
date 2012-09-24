LOCAL_PATH:= $(call my-dir)

OML_VERSION=@version@

include $(CLEAR_VARS)

LOCAL_MODULE := example-oml2
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES:= $(call all-c-files-under,. )
LOCAL_C_INCLUDES += $(common_target_c_includes) \
		    external/liboml2/client \
		    external/liboml2/ocomm \
		    external/liboml2/shared \
		    external/libpopt
LOCAL_CFLAGS := $(common_target_cflags)
LOCAL_LDFLAGS := -lpopt -loml2

include $(BUILD_EXECUTABLE)
