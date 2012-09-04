LOCAL_PATH:= $(call my-dir)

OML_VERSION=@version@

include $(CLEAR_VARS)

LOCAL_MODULE := oml2-proxy-server
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES:= $(call all-c-files-under,.)
LOCAL_C_INCLUDES += external/liboml2/client \
		    external/liboml2/ocomm \
		    external/liboml2/shared 
LOCAL_CFLAGS := $(common_target_cflags) -DVERSION=\"$(OML_VERSION)\"
LOCAL_LDFLAGS := -loml2 -lpopt -lm -lz

include $(BUILD_EXECUTABLE)
