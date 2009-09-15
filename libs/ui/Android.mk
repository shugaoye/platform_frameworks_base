LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	Camera.cpp \
	CameraParameters.cpp \
	EGLDisplaySurface.cpp \
	EGLNativeWindowSurface.cpp \
	EventHub.cpp \
	EventRecurrence.cpp \
	KeyLayoutMap.cpp \
	KeyCharacterMap.cpp \
	ICamera.cpp \
	ICameraClient.cpp \
	ICameraService.cpp \
	IOverlay.cpp \
	ISurfaceComposer.cpp \
	ISurface.cpp \
	ISurfaceFlingerClient.cpp \
	LayerState.cpp \
	Overlay.cpp \
	PixelFormat.cpp \
	Rect.cpp \
	Region.cpp \
	Surface.cpp \
	SurfaceComposerClient.cpp \
	SurfaceFlingerSynchro.cpp 

LOCAL_SHARED_LIBRARIES := \
	libcorecg \
	libcutils \
	libutils \
	libpixelflinger \
	libhardware \
	libhardware_legacy

LOCAL_SRC_FILES += EGLKMSSurface.cpp
LOCAL_SHARED_LIBRARIES += libdrm
LOCAL_C_INCLUDES += \
	external/drm/shared-core \
	external/drm/libdrm

LOCAL_MODULE:= libui

include $(BUILD_SHARED_LIBRARY)
