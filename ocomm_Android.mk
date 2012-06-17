LOCAL_PATH:= $(call my-dir)

common_src := \
	eventloop.c \
	log.c \
	mt_queue.c \
	queue.c \
	socket.c \
	socket_group.c

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= $(common_src)
LOCAL_C_INCLUDES := $(common_target_c_includes)
LOCAL_CFLAGS := $(common_target_cflags)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libocomm

include $(BUILD_SHARED_LIBRARY)
