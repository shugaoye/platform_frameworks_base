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

#ifndef ANDROID_GEM_HARDWARE_H
#define ANDROID_GEM_HARDWARE_H

#include <stdint.h>
#include <sys/types.h>

#include <utils/RefBase.h>
#include <utils/threads.h>
#include <utils/KeyedVector.h>
#include <utils/IBinder.h>
#include <utils/MemoryDealer.h>

#include <ui/ISurfaceComposer.h>
#include "GPUHardware/GPUHardware.h"

namespace android {

// ---------------------------------------------------------------------------

class GEMHardware : public GPUHardwareInterface, public IBinder::DeathRecipient
{
public:
            GEMHardware();
    virtual ~GEMHardware();
    
    virtual sp<MemoryDealer> request(int pid);
    virtual status_t request(int pid, 
            const sp<IGPUCallback>& callback,
            ISurfaceComposer::gpu_info_t* gpu);

    virtual void revoke(int pid);
    virtual status_t friendlyRevoke();
    virtual void unconditionalRevoke();
    
    virtual pid_t getOwner() const { return 0; }
    virtual sp<SimpleBestFitAllocator> getAllocator() const { return 0; }

private:
    
    struct Client {
        pid_t       pid;
        int         count;
    };
    
    KeyedVector<pid_t, Client> mClients;
    DefaultKeyedVector< wp<IBinder>, pid_t > mRegisteredClients;

    Client& getClientLocked(pid_t pid);
    sp<MemoryDealer> requestLocked(Client &client);
    void registerCallbackLocked(const sp<IGPUCallback>& callback,
        Client& client);
    void binderDied(const wp<IBinder>& who);

    mutable Mutex           mLock;
};

// ---------------------------------------------------------------------------
}; // namespace android

#endif // ANDROID_GEM_HARDWARE_H
