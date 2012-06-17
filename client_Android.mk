LOCAL_PATH:= $(call my-dir)

common_src := \
	api.c \
	filter.c \
	init.c \
	misc.c \
	text_writer.c \
	bin_writer.c \
	file_stream.c \
	net_stream.c \
	buffered_writer.c \
	parse_config.c \
	filter/factory.c \
	filter/first_filter.c \
	filter/last_filter.c \
	filter/average_filter.c \
	filter/histogram_filter.c \
	filter/stddev_filter.c \
	filter/sum_filter.c \
	filter/delta_filter.c

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= $(common_src)
LOCAL_C_INCLUDES := $(common_target_c_includes) $(LOCAL_PATH)/../ocomm $(LOCAL_PATH)/../shared $(LOCAL_PATH)/../../libxml2/include $(LOCAL_PATH)/../../icu4c/common/
LOCAL_CFLAGS := $(common_target_cflags) -DVERSION=\"2.7.0\"
LOCAL_LDFLAGS := -locomm -lxml2 -lm
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := liboml2
LOCAL_STATIC_LIBRARIES := libshared

include $(BUILD_SHARED_LIBRARY)
