LOCAL_PATH:= $(call my-dir)

common_src := \
	marshal.c \
	mbuf.c \
	cbuf.c \
	mstring.c \
	mem.c \
	oml_value.c \
	log.c \
	validate.c \
	schema.c \
	headers.c \
	binary.c \
	text.c \
	util.c

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= $(common_src)
LOCAL_C_INCLUDES := $(common_target_c_includes) $(LOCAL_PATH)/../ocomm $(LOCAL_PATH)/../client
LOCAL_CFLAGS := $(common_target_cflags)
LOCAL_LDFLAGS := -locomm -lxml2 -lm
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libshared

include $(BUILD_STATIC_LIBRARY)
