/*
 * Copyright (C) 2009 0xlab.org
 * Authored by Chia-I Wu <olv@0xlab.org>
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

#ifndef ANDROID_MEMORY_GEM_H
#define ANDROID_MEMORY_GEM_H

#include <utils/IMemory.h>

namespace android {

// ----------------------------------------------------------------------------
void	memory_gem_promote(sp<IMemoryHeap> &heap);
// ----------------------------------------------------------------------------

}; // namespace android

#endif // ANDROID_MEMORY_GEM_H
