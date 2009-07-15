/*
**
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
#include <fcntl.h>
#include <sys/mman.h>

#define VIDEO_DEVICE        "/dev/video0"
#define MIN_WIDTH           640

#define MIN_HEIGHT          480
#define PIXEL_FORMAT        V4L2_PIX_FMT_YUYV

namespace android {

wp<CameraHardwareInterface> CameraHardware::singleton;

CameraHardware::CameraHardware()
                  : mParameters(),
                    mHeap(0),
                    mPreviewHeap(0),
                    mRawHeap(0),
					mPreviewFrameSize(0),
                    mRawPictureCallback(0),
                    mJpegPictureCallback(0),
                    mPictureCallbackCookie(0),
                    mPreviewCallback(0),
                    mPreviewCallbackCookie(0),
                    mAutoFocusCallback(0),
                    mAutoFocusCallbackCookie(0),
                    mCurrentPreviewFrame(0),
                    previewStopped(true),
                    nQueued(0),
                    nDequeued(0)
{
    initDefaultParameters();
}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

    p.setPreviewSize(MIN_WIDTH, MIN_HEIGHT);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("yuv422sp");

    p.setPictureSize(MIN_WIDTH, MIN_HEIGHT);
    p.setPictureFormat("jpeg");

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
    return mHeap;
}

sp<IMemoryHeap> CameraHardware::getRawHeap() const
{
    return mRawHeap;
}

// ---------------------------------------------------------------------------

int CameraHardware::previewThread()
{
    if (!previewStopped) {
        // Get preview frame
        camera.GrabPreviewFrame(mHeap->getBase());
        mPreviewCallback(mBuffer, mPreviewCallbackCookie);
    }

    return NO_ERROR;
}

status_t CameraHardware::startPreview(preview_callback cb, void* user)
{
    int width, height;

    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        //already running
        return INVALID_OPERATION;
    }

    camera.Open(VIDEO_DEVICE, MIN_WIDTH, MIN_HEIGHT, PIXEL_FORMAT);

    mPreviewFrameSize = MIN_WIDTH * MIN_HEIGHT * 2;

    mHeap = new MemoryHeapBase(mPreviewFrameSize);
    mBuffer = new MemoryBase(mHeap, 0, mPreviewFrameSize);

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
    return UNKNOWN_ERROR;
}

void CameraHardware::stopRecording()
{
}

bool CameraHardware::recordingEnabled()
{
    return false;
}

void CameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
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
    unsigned char *frame;
    int bufferSize;
    int w,h;
    int ret;
    struct v4l2_buffer buffer;
    struct v4l2_format format;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_requestbuffers creqbuf;
    struct v4l2_capability cap;

    if (mShutterCallback)
        mShutterCallback(mPictureCallbackCookie);

    mParameters.getPictureSize(&w, &h);
    LOGD("Picture Size: Width = %d \t Height = %d", w, h);

    int width, height;
    mParameters.getPictureSize(&width, &height);
    camera.Open(VIDEO_DEVICE, MIN_WIDTH, MIN_HEIGHT, PIXEL_FORMAT);
    camera.Init();
    camera.StartStreaming();

    if (mRawPictureCallback) {
        LOGD("mRawPictureCallback");

//    	mRawPictureCallback(camera.GrabRawFrame(), mPictureCallbackCookie);
    }

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
    //if (createThread(beginPictureThread, this) == false)
    //    return -1;

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

    mParameters = params;

    // Set to fixed sizes
    mParameters.setPreviewSize(MIN_WIDTH,MIN_HEIGHT);

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
    close(camera_device);
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
