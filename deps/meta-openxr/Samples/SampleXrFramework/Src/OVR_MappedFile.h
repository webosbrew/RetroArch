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

Filename    :   OVR_MappedFile.h
Content     :   Cross-platform memory-mapped file wrapper.
Created     :   May 12, 2014
Authors     :   Chris Taylor

*************************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

/*
    Memory-mapped files are a fairly good compromise between performance and flexibility.

    Compared with asynchronous io, memory-mapped files are:
        + Much easier to implement in a portable way
        + Automatically paged in and out of RAM
        + Automatically read-ahead cached

    When asynch IO is not available or blocking is acceptable then this is a
    great alternative with low overhead and similar performance.

    For random file access, use MappedView with a MappedFile that has been
    opened with random_access = true.  Random access is usually used for a
    database-like file type, which is much better implemented using asynch IO.
*/

namespace OVRFW {

// Read-only memory mapped file
class MappedFile {
    friend class MappedView;

   public:
    MappedFile();
    ~MappedFile();

    // Opens the file for shared read-only access with other applications
    // Returns false on error (file not found, etc)
    bool OpenRead(const char* path, bool readAhead = false, bool noCache = false);

    // Creates and opens the file for exclusive read/write access
    bool OpenWrite(const char* path, size_t size);

    void Close();

    [[nodiscard]] bool IsReadOnly() const {
        return readOnly_;
    }
    [[nodiscard]] size_t GetLength() const {
        return length_;
    }
    [[nodiscard]] bool IsValid() const {
        return (length_ != 0);
    }

   private:
    int file_;
    bool readOnly_;
    size_t length_;
};

// View of a portion of the memory mapped file
class MappedView {
   public:
    MappedView();
    ~MappedView();

    bool Open(MappedFile* file); // Returns false on error
    uint8_t* MapView(
        size_t offset = 0,
        uint32_t length = 0); // Returns 0 on error, 0 length means whole file
    void Close();

    [[nodiscard]] bool IsValid() const {
        return (data_ != nullptr);
    }
    [[nodiscard]] size_t GetOffset() const {
        return offset_;
    }
    [[nodiscard]] uint32_t GetLength() const {
        return length_;
    }
    MappedFile* GetFile() {
        return file_;
    }
    uint8_t* GetFront() {
        return data_;
    }

   private:
    void* map_;

    MappedFile* file_;
    uint8_t* data_;
    size_t offset_;
    uint32_t length_;
};

} // namespace OVRFW
