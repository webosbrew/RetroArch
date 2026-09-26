/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * Licensed under the Oculus SDK License Agreement (the "License");
 * you may not use the Oculus SDK except in compliance with the License,
 * which is provided at the time of installation or download, or which
 * otherwise accompanies this software in either electronic or hard copy form.
 *
 * You may obtain a copy of the License at
 * https://developer.oculus.com/licenses/oculussdk/
 *
 * Unless required by applicable law or agreed to in writing, the Oculus SDK
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/************************************************************************************

Filename    :   OVR_MappedFile.cpp
Content     :   Cross-platform memory-mapped file wrapper.
Created     :   May 12, 2014
Authors     :   Chris Taylor

*************************************************************************************/

#include "OVR_MappedFile.h"
#include "OVR_Types.h"

#if defined(OVR_OS_ANDROID)

#if defined(OVR_OS_ANDROID)
// disable warnings on implicit type conversion where value may be changed by conversion for
// sys/stat.h
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(OVR_OS_ANDROID)
// restore warnings on implicit type conversion where value may be changed by conversion for
// sys/stat.h
#pragma GCC diagnostic pop
#endif

namespace OVRFW {

static uint32_t defaultAllocationGranularity = 65536;

static uint32_t GetAllocationGranularity() {
    uint32_t allocGran = 0;

#if defined(OVR_OS_WIN32)

    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    allocGran = sys_info.dwAllocationGranularity;

#elif defined(OVR_OS_MAC) || defined(OVR_OS_IPHONE)

    allocGran = (uint32_t)getpagesize();

#elif defined(_SC_PAGE_SIZE)

    allocGran = (uint32_t)sysconf(_SC_PAGE_SIZE);

#endif

    return (allocGran > 0) ? allocGran : defaultAllocationGranularity;
}

/*
    MappedFile
*/

MappedFile::MappedFile() {
    length_ = 0;
    file_ = -1;
}

MappedFile::~MappedFile() {
    Close();
}

bool MappedFile::OpenRead(const char* path, bool readAhead, bool noCache) {
    Close();

    readOnly_ = true;

    // Don't allow private files to be read by other applications.
    file_ = open(path, O_RDONLY);

    if (file_ == -1) {
        return false;
    } else {
        length_ = lseek(file_, 0, SEEK_END);

        if (length_ <= 0) {
            return false;
        } else {
#if defined(F_RDAHEAD)
            if (readAhead) {
                fcntl(file_, F_RDAHEAD, 1);
            }
#endif

#if defined(F_NOCACHE)
            if (noCache) {
                fcntl(file_, F_NOCACHE, 1);
            }
#endif
        }
    }

    return true;
}

bool MappedFile::OpenWrite(const char* path, size_t size) {
    Close();

    readOnly_ = false;
    length_ = size;

    // Don't allow private files to be read or written by
    // other applications.
    file_ = open(path, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0660);

    if (file_ == -1) {
        return false;
    } else {
        if (-1 == lseek(file_, size - 1, SEEK_SET)) {
            return false;
        } else {
            if (1 != write(file_, "", 1)) {
                return false;
            }
        }
    }

    return true;
}

void MappedFile::Close() {
    if (file_ != -1) {
        close(file_);
        file_ = -1;
    }

    length_ = 0;
}

/*
    MappedView
*/

MappedView::MappedView() {
    data_ = nullptr;
    length_ = 0;
    offset_ = 0;
    file_ = nullptr;
    map_ = MAP_FAILED;
}

MappedView::~MappedView() {
    Close();
}

bool MappedView::Open(MappedFile* file) {
    Close();

    if (!file || !file->IsValid()) {
        return false;
    }

    file_ = file;
    return true;
}

uint8_t* MappedView::MapView(size_t offset, uint32_t length) {
    if (length == 0) {
        length = static_cast<uint32_t>(file_->GetLength());
    }

    if (offset) {
        uint32_t granularity = GetAllocationGranularity();

        // Bring offset back to the previous allocation granularity
        uint32_t mask = granularity - 1;
        uint32_t masked = (uint32_t)offset & mask;
        if (masked) {
            offset -= masked;
            length += masked;
        }
    }

    int prot = PROT_READ;
    if (!file_->readOnly_) {
        prot |= PROT_WRITE;
    }

    // Use MAP_PRIVATE so that memory is not exposed to other processes.
    map_ = mmap(nullptr, length, prot, MAP_PRIVATE, file_->file_, offset);

    if (map_ == MAP_FAILED) {
        return nullptr;
    }

    data_ = reinterpret_cast<uint8_t*>(map_);

    offset_ = offset;
    length_ = length;

    return data_;
}

void MappedView::Close() {
    if (map_ != MAP_FAILED) {
        munmap(map_, length_);
        map_ = MAP_FAILED;
    }
    data_ = nullptr;
    length_ = 0;
    offset_ = 0;
}

} // namespace OVRFW

#endif // defined(OVR_OS_ANDROID)
