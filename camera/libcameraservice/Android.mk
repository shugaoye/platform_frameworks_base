LOCAL_PATH:= $(call my-dir)

#
# libcamera
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=               \
    CameraHardware.cpp         \
    V4L2Camera.cpp

LOCAL_CFLAGS += -Iexternal/jpeg

LOCAL_MODULE:= libcamera

LOCAL_SHARED_LIBRARIES:= \
    libui \
    libutils \
    libcutils

LOCAL_STATIC_LIBRARIES:= \
    libjpeg

include $(BUILD_SHARED_LIBRARY)

#
# libcameraservice
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=               \
    CameraService.cpp

LOCAL_SHARED_LIBRARIES:= \
    libui \
    libutils \
    libcutils \
    libmedia

LOCAL_MODULE:= libcameraservice

LOCAL_CFLAGS+=-DLOG_TAG=\"CameraService\"

ifeq ($(USE_CAMERA_STUB), true)
LOCAL_STATIC_LIBRARIES += libcamerastub
LOCAL_CFLAGS += -include CameraHardwareStub.h
else
LOCAL_SHARED_LIBRARIES += libcamera 
endif

include $(BUILD_SHARED_LIBRARY)

