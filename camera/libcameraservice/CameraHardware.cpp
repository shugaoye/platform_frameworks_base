/*
**
** Copyright (C) 2009 0xlab.org - http://0xlab.org/
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "CameraHardware"
#include <utils/Log.h>

#include "CameraHardware.h"
#include "converter.h"
#include <fcntl.h>
#include <sys/mman.h>

#define VIDEO_DEVICE        "/dev/video0"
#define PREVIEW_WIDTH           352
#define PREVIEW_HEIGHT          288
#define PICTURE_WIDTH       1600   /* 2MP */
#define PICTURE_HEIGHT      1200   /* 2MP */
#define PIXEL_FORMAT        V4L2_PIX_FMT_YUYV

#include <cutils/properties.h>
#ifndef UNLIKELY
#define UNLIKELY(exp) (__builtin_expect( (exp) != 0, false ))
#endif
static int mDebugFps = 0;

namespace android {

wp<CameraHardwareInterface> CameraHardware::singleton;

CameraHardware::CameraHardware()
                  : mParameters(),
                    mHeap(0),
                    mPreviewHeap(0),
                    mRawHeap(0),
                    mPreviewFrameSize(0),
                    mPictureFrameSize(0),
                    mRecordingFrameSize(0),
                    mRecordingCallback(0),
                    mRecordingCallbackCookie(0),
                    mRawPictureCallback(0),
                    mJpegPictureCallback(0),
                    mPictureCallbackCookie(0),
                    mPreviewCallback(0),
                    mPreviewCallbackCookie(0),
                    mAutoFocusCallback(0),
                    mAutoFocusCallbackCookie(0),
                    previewStopped(true)
{
    initDefaultParameters();
    /* whether prop "debug.camera.showfps" is enabled or not */
    char value[PROPERTY_VALUE_MAX];
    property_get("debug.camera.showfps", value, "0");
    mDebugFps = atoi(value);
    LOGD_IF(mDebugFps, "showfps enabled");
}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

    p.setPreviewSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    p.setPreviewFrameRate(DEFAULT_FRAME_RATE);
    p.setPreviewFormat("yuv422sp");
    p.setPictureFormat("jpeg");
    p.setPictureSize(PICTURE_WIDTH, PICTURE_HEIGHT);

    p.set("jpeg-quality", "100"); // maximum quality
    p.set("picture-size-values", "1600x1200,1024x768,640x480,352x288");

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }
}

CameraHardware::~CameraHardware()
{
    singleton.clear();
}

sp<IMemoryHeap> CameraHardware::getPreviewHeap() const
{
    return mPreviewHeap;
}

sp<IMemoryHeap> CameraHardware::getRawHeap() const
{
    return mRawHeap;
}

// ---------------------------------------------------------------------------
static void showFPS(const char *tag)
{
    static int mFrameCount = 0;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    if (!(mFrameCount & 0x1F)) {
        nsecs_t now = systemTime();
        nsecs_t diff = now - mLastFpsTime;
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
        LOGD("[%s] %d Frames, %f FPS", tag, mFrameCount, mFps);
    }
}

int CameraHardware::previewThread()
{
    Mutex::Autolock lock(mPreviewLock);
    if (!previewStopped) {

        camera.GrabRawFrame(mRawHeap->getBase());

        if (mRecordingCallback) {
            yuyv422_to_yuv420((unsigned char *)mRawHeap->getBase(),
                              (unsigned char *)mRecordingHeap->getBase(),
                              PREVIEW_WIDTH, PREVIEW_HEIGHT);
            nsecs_t timeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
            mRecordingCallback(timeStamp, mRecordingBuffer, mRecordingCallbackCookie);

            camera.convert((unsigned char *) mRawHeap->getBase(),
                           (unsigned char *) mPreviewHeap->getBase(),
                           PREVIEW_WIDTH, PREVIEW_HEIGHT);
            mPreviewCallback(mRecordingBuffer, mPreviewCallbackCookie);

            if (UNLIKELY(mDebugFps)) {
                showFPS("Recording");
            }
        }
        else {
            camera.convert((unsigned char *) mRawHeap->getBase(),
                           (unsigned char *) mPreviewHeap->getBase(),
                           PREVIEW_WIDTH, PREVIEW_HEIGHT);

            yuyv422_to_yuv420((unsigned char *)mRawPicHeap->getBase(),
                              (unsigned char *)mHeap->getBase(),
                              PICTURE_WIDTH, PICTURE_HEIGHT);
            mPreviewCallback(mBuffer, mPreviewCallbackCookie);

            if (UNLIKELY(mDebugFps)) {
                showFPS("Preview");
            }
        }
    }

    return NO_ERROR;
}

status_t CameraHardware::startPreview(preview_callback cb, void* user)
{
    int width, height;

    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        //already running
        LOGD("startPreview:  but already running");
        return INVALID_OPERATION;
    }

    if (camera.Open(VIDEO_DEVICE, PREVIEW_WIDTH, PREVIEW_HEIGHT, PIXEL_FORMAT) < 0) {
	LOGE("startPreview failed: cannot open device.");
        return UNKNOWN_ERROR;
    }

    mPreviewFrameSize = PREVIEW_WIDTH * PREVIEW_HEIGHT * 2;
    mPictureFrameSize = PICTURE_WIDTH * PICTURE_HEIGHT * 2;// to verify the picture to preview

    mHeap = new MemoryHeapBase(mPictureFrameSize);
    mBuffer = new MemoryBase(mHeap, 0, mPictureFrameSize);

    mPreviewHeap = new MemoryHeapBase(mPreviewFrameSize);
    mPreviewBuffer = new MemoryBase(mPreviewHeap, 0, mPreviewFrameSize);

    mRecordingHeap = new MemoryHeapBase(mPreviewFrameSize);
    mRecordingBuffer = new MemoryBase(mRecordingHeap, 0, mPreviewFrameSize);

    mRawHeap = new MemoryHeapBase(mPreviewFrameSize);
    mRawBuffer = new MemoryBase(mRawHeap, 0, mPreviewFrameSize);

    mRawPicHeap = new MemoryHeapBase(mPictureFrameSize);
    mRawPicBuffer = new MemoryBase(mRawPicHeap, 0, mPictureFrameSize);

    camera.Init();
    camera.StartStreaming();

    previewStopped = false;

    mPreviewCallback = cb;
    mPreviewCallbackCookie = user;
    mPreviewThread = new PreviewThread(this);

    return NO_ERROR;
}

void CameraHardware::stopPreview()
{
    sp<PreviewThread> previewThread;

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewStopped = true;
    }

    if (mPreviewThread != 0) {
        camera.Uninit();
        camera.StopStreaming();
        camera.Close();
    }

    {
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
}

bool CameraHardware::previewEnabled()
{
    return mPreviewThread != 0;
}

status_t CameraHardware::startRecording(recording_callback cb, void* user)
{
    LOGD("startRecording");
    mRecordingLock.lock();
    mRecordingCallback = cb;
    mRecordingCallbackCookie = user;
    mRecordingLock.unlock();
    return NO_ERROR;

}

void CameraHardware::stopRecording()
{
    mRecordingLock.lock();
    mRecordingCallback = NULL;
    mRecordingCallbackCookie = NULL;
    mRecordingLock.unlock();
}

bool CameraHardware::recordingEnabled()
{
    return mRecordingCallback !=0;
}

void CameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
    if (UNLIKELY(mDebugFps)) {
        showFPS("Recording");
    }
    return;
}

// ---------------------------------------------------------------------------

int CameraHardware::beginAutoFocusThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->autoFocusThread();
}

int CameraHardware::autoFocusThread()
{
    if (mAutoFocusCallback != NULL) {
        mAutoFocusCallback(true, mAutoFocusCallbackCookie);
        mAutoFocusCallback = NULL;
        return NO_ERROR;
    }
    return UNKNOWN_ERROR;
}

status_t CameraHardware::autoFocus(autofocus_callback af_cb,
                                       void *user)
{
    Mutex::Autolock lock(mLock);

    if (mAutoFocusCallback != NULL) {
        return mAutoFocusCallback == af_cb ? NO_ERROR : INVALID_OPERATION;
    }

    mAutoFocusCallback = af_cb;
    mAutoFocusCallbackCookie = user;
    if (createThread(beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

/*static*/ int CameraHardware::beginPictureThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->pictureThread();
}

int CameraHardware::pictureThread()
{
    int ret;

    if (mShutterCallback)
        mShutterCallback(mPictureCallbackCookie);

    camera.Open(VIDEO_DEVICE, PICTURE_WIDTH, PICTURE_HEIGHT, PIXEL_FORMAT);
    camera.Init();
    camera.StartStreaming();

    if (mJpegPictureCallback) {
        LOGD ("mJpegPictureCallback");

        mJpegPictureCallback(camera.GrabJpegFrame(), mPictureCallbackCookie);
    }

    camera.Uninit();
    camera.StopStreaming();
    camera.Close();
    return NO_ERROR;
}

status_t CameraHardware::takePicture(shutter_callback shutter_cb,
                                         raw_callback raw_cb,
                                         jpeg_callback jpeg_cb,
                                         void* user)
{
    stopPreview();
    mShutterCallback = shutter_cb;
    mRawPictureCallback = raw_cb;
    mJpegPictureCallback = jpeg_cb;
    mPictureCallbackCookie = user;
    pictureThread();

    return NO_ERROR;
}

status_t CameraHardware::cancelPicture(bool cancel_shutter,
                                           bool cancel_raw,
                                           bool cancel_jpeg)
{
    if (cancel_shutter) mShutterCallback = NULL;
    if (cancel_raw) mRawPictureCallback = NULL;
    if (cancel_jpeg) mJpegPictureCallback = NULL;

    return NO_ERROR;
}

status_t CameraHardware::dump(int fd, const Vector<String16>& args) const
{
    return NO_ERROR;
}

status_t CameraHardware::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);

    if (strcmp(params.getPreviewFormat(), "yuv422sp") != 0) {
        LOGE("Only yuv422sp preview is supported");
        return -1;
    }

    if (strcmp(params.getPictureFormat(), "jpeg") != 0) {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    int w, h;
    int framerate;

    params.getPreviewSize(&w, &h);
    framerate = params.getPreviewFrameRate();
    LOGD("PREVIEW SIZE: w=%d h=%d framerate=%d", w, h, framerate);

    params.getPictureSize(&w, &h);
    LOGD("requested size %d x %d", w, h);

    mParameters = params;

    // Set to fixed sizes
    mParameters.setPreviewSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    mParameters.setPictureSize(PICTURE_WIDTH, PICTURE_HEIGHT);

    return NO_ERROR;
}

CameraParameters CameraHardware::getParameters() const
{
    CameraParameters params;

    {
        Mutex::Autolock lock(mLock);
        params = mParameters;
    }

    return params;
}

void CameraHardware::release()
{
}

sp<CameraHardwareInterface> CameraHardware::createInstance()
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardware());
    singleton = hardware;
    return hardware;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    return CameraHardware::createInstance();
}
}; // namespace android
