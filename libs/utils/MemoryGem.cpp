/*
 * Copyright (C) 2009 0xlab.org
 * Authored by Chia-I Wu <olv@0xlab.org>
 *
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "MemoryGem"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/mman.h>

#include <utils/IMemory.h>
#include <utils/KeyedVector.h>
#include <utils/threads.h>
#include <utils/Atomic.h>
#include <utils/Parcel.h>
#include <utils/CallStack.h>

#include <utils/MemoryGem.h>
#include <i915_drm.h>

#define VERBOSE   0

namespace android {
// ---------------------------------------------------------------------------

enum {
    /* must be kept in sync with IMemory.cpp */
    HEAP_ID = IBinder::FIRST_CALL_TRANSACTION,
    HEAP_ID_GEM
};

class BpMemoryHeapGem : public BpInterface<IMemoryHeap>
{
public:
    BpMemoryHeapGem(const sp<IBinder>& impl);
    virtual ~BpMemoryHeapGem();

    virtual int getHeapID() const;
    virtual void* getBase() const;
    virtual size_t getSize() const;
    virtual uint32_t getFlags() const;

    virtual uint32_t getGemName() const;
    virtual uint32_t getGemHandle() const;

private:
    void assertOpened() const;
    void assertMapped() const;

    mutable volatile int mGemFd;
    mutable uint32_t    mGemName;
    mutable uint32_t    mGemHandle;

    mutable void*       mBase;
    mutable size_t      mSize;
    mutable uint32_t    mFlags;
    mutable Mutex       mLock;
};

// ---------------------------------------------------------------------------

void memory_gem_promote(sp<IMemoryHeap> &heap)
{
	sp<IMemoryHeap> tmp;

	if (!heap.get())
		return;

	tmp = new BpMemoryHeapGem(heap->asBinder());
	if (tmp->getGemName()) {
		LOGI("%p promoted to MemoryHeapGem", heap.get());
		heap = tmp;
	}
}

// ---------------------------------------------------------------------------
BpMemoryHeapGem::BpMemoryHeapGem(const sp<IBinder>& impl)
    : BpInterface<IMemoryHeap>(impl),
        mGemFd(-1), mGemName(0), mGemHandle(0), mBase(MAP_FAILED), mSize(0), mFlags(0)
{
}

BpMemoryHeapGem::~BpMemoryHeapGem() {
    if (mGemFd != -1) {
        if (mGemHandle != 0) {
            struct drm_gem_close close_arg;
            close_arg.handle = mGemHandle;
            ioctl(mGemFd, DRM_IOCTL_GEM_CLOSE, &close_arg);
        }

        // by construction we're the last one
        if (mBase != MAP_FAILED)
            munmap(mBase, mSize);

        close(mGemFd);
    }
}

void BpMemoryHeapGem::assertMapped() const
{
    assertOpened();
    if (mGemFd != -1) {
        int access = PROT_READ;
        if (!(mFlags & READ_ONLY)) {
            access |= PROT_WRITE;
        }

	Mutex::Autolock _l(mLock);
        if (mBase == MAP_FAILED) {
            struct drm_i915_gem_mmap gmap;
            int err;

            memset(&gmap, 0, sizeof(gmap));

            gmap.handle = mGemHandle;
            gmap.size = mSize;
            err = ioctl(mGemFd, DRM_IOCTL_I915_GEM_MMAP, &gmap);
            if (!err)
                mBase = (void *) gmap.addr_ptr;

            if (mBase == MAP_FAILED) {
                LOGE("cannot map BpMemoryHeapGem (binder=%p), size=%d, fd=%d (%s)",
                        asBinder().get(), mSize, mGemFd, strerror(errno));
            }
        }
    }
}

void BpMemoryHeapGem::assertOpened() const
{
    if (mGemFd == -1) {

        // remote call without mLock held, worse case scenario, we end up
        // calling transact() from multiple threads, but that's not a problem,
        // only mmap below must be in the critical section.

        Parcel data, reply;

        data.writeInterfaceToken(IMemoryHeap::getInterfaceDescriptor());
        status_t err = remote()->transact(HEAP_ID_GEM, data, &reply);
        int parcel_fd = reply.readFileDescriptor();
        ssize_t size = reply.readInt32();
        uint32_t flags = reply.readInt32();
        uint32_t gem_name = reply.readInt32();

        LOGE_IF(err, "binder=%p transaction failed fd=%d, size=%ld, err=%d (%s)",
                asBinder().get(), parcel_fd, size, err, strerror(-err));

        int fd = dup( parcel_fd );
        LOGE_IF(fd==-1, "cannot dup fd=%d, size=%ld, err=%d (%s)",
                parcel_fd, size, err, strerror(errno));

        Mutex::Autolock _l(mLock);
        if (mGemFd == -1) {
            struct drm_gem_open open_arg;
            open_arg.name = gem_name;
            int ret = ioctl(fd, DRM_IOCTL_GEM_OPEN, &open_arg);
            if (ret != 0) {
                LOGE("cannot open gem object: %s", strerror(errno));
                close(fd);
            } else {
                android_atomic_write(fd, &mGemFd);

                mGemName = gem_name;
                mGemHandle = open_arg.handle;
                mSize = size;
                mFlags = flags;
            }
        }
    }
}

int BpMemoryHeapGem::getHeapID() const {
    assertOpened();
    return mGemFd;
}

void* BpMemoryHeapGem::getBase() const {
    assertMapped();
    return mBase;
}

size_t BpMemoryHeapGem::getSize() const {
    assertOpened();
    return mSize;
}

uint32_t BpMemoryHeapGem::getFlags() const {
    assertOpened();
    return mFlags;
}

uint32_t BpMemoryHeapGem::getGemName() const {
    assertOpened();
    return mGemName;
}

uint32_t BpMemoryHeapGem::getGemHandle() const {
    assertOpened();
    return mGemHandle;
}

// ---------------------------------------------------------------------------
}; // namespace android
