LOCAL_PATH:= $(call my-dir)

OML_VERSION=@version@

include $(CLEAR_VARS)

LOCAL_MODULE := liboml2
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(call all-c-files-under,client ocomm shared)
LOCAL_C_INCLUDES := $(common_target_c_includes) \
	$(LOCAL_PATH)/client $(LOCAL_PATH)/ocomm $(LOCAL_PATH)/shared \
	external/libxml2/include external/icu4c/common
LOCAL_CFLAGS := $(common_target_cflags) -DVERSION=\"$(OML_VERSION)\"
LOCAL_LDFLAGS := -lxml2 -lm

include $(BUILD_SHARED_LIBRARY)
