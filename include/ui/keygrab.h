/*
 * Copyright (C) 2010 The Android-X86 Open Source Project
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
 *
 * Aurthor: Yi Sun
 * The code is based on the idea from
 * Filippo Pagin <filippo.pagin@gmail.com>
 */


#ifndef __KEYGRAB_H__
#define __KEYGRAB_H__

namespace android {
/*
 * The idea is to set a global flag when surfaceflinger to release or grab
 * a screen. So that the eventhub can stop to push events into
 * upper layer after surfaceflinger released the screen.
 * The keygrab class is just used to set this global flag.
 */

class keyGrab {

 private:
    static bool keygrab;

 public:
    static void setGrabOff() {
        keygrab = false;
    }

    static void setGrabOn() {
        keygrab = true;
    }

    static bool isGrabOn() {
        return keygrab;
    }

};
};  // namespace android
#endif /* __KEYGRAB_H__ */
