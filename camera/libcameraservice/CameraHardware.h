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

#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_H

#include <utils/threads.h>
#include <ui/CameraHardwareInterface.h>
#include <utils/MemoryBase.h>
#include <utils/MemoryHeapBase.h>
#include <utils/threads.h>

#include <jpeglib.h>
#include "V4L2Camera.h"

namespace android {

class CameraHardware : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual status_t    startPreview(preview_callback cb, void* user);
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording(recording_callback cb, void* user);
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus(autofocus_callback, void *user);
    virtual status_t    takePicture(shutter_callback,
                                    raw_callback,
                                    jpeg_callback,
                                    void* user);
    virtual status_t    cancelPicture(bool cancel_shutter,
                                      bool cancel_raw,
                                      bool cancel_jpeg);
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual void release();

    static sp<CameraHardwareInterface> createInstance();

private:
                        CameraHardware();
    virtual             ~CameraHardware();

    static wp<CameraHardwareInterface> singleton;

    static const int kBufferCount = 4;

    class PreviewThread : public Thread {
        CameraHardware* mHardware;
    public:
        PreviewThread(CameraHardware* hw)
            : Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThread();
            // loop until we need to quit
            return true;
        }
    };

    void initDefaultParameters();
    bool initHeapLocked();

    int previewThread();

    static int beginAutoFocusThread(void *cookie);
    int autoFocusThread();

    static int beginPictureThread(void *cookie);
    int pictureThread();

    mutable Mutex       mLock;
    mutable Mutex       mPreviewLock;

    CameraParameters    mParameters;

    sp<MemoryHeapBase>  mHeap;         // format: 420
    sp<MemoryBase>      mBuffer;
    sp<MemoryHeapBase>  mRawPicHeap;      // format: 422
    sp<MemoryBase>      mRawPicBuffer;

    sp<MemoryHeapBase>  mPreviewHeap;  // format: 565
    sp<MemoryBase>      mPreviewBuffer;
    sp<MemoryHeapBase>  mRawHeap;      // format: 422
    sp<MemoryBase>      mRawBuffer;

    bool                mPreviewRunning;
    int                 mPreviewFrameSize;
    int                 mPictureFrameSize;

    shutter_callback    mShutterCallback;
    raw_callback        mRawPictureCallback;
    jpeg_callback       mJpegPictureCallback;
    void                *mPictureCallbackCookie;

    sp<PreviewThread>   mPreviewThread;
    preview_callback    mPreviewCallback;
    void                *mPreviewCallbackCookie;

    autofocus_callback  mAutoFocusCallback;
    void                *mAutoFocusCallbackCookie;

    sp<MemoryHeapBase>  mRecordingHeap;
    sp<MemoryBase>      mRecordingBuffer;

    Mutex               mRecordingLock;
    int                 mRecordingFrameSize;
    recording_callback  mRecordingCallback;
    void                *mRecordingCallbackCookie;

    bool		previewStopped;
    V4L2Camera		camera;
};

}; // namespace android

#endif
