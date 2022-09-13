#include "d3d9_mem.h"
#include "../util/util_string.h"
#include "../util/util_math.h"
#include "../util/log/log.h"
#include "../util/util_likely.h"
#include <utility>

#ifdef UNMAP_V1

namespace dxvk {

#ifdef D3D9_ALLOW_UNMAPPING

#else

#ifdef D3D9_ALLOW_UNMAPPING
#include <sysinfoapi.h>
#else
#include <stdlib.h>
#endif

namespace dxvk {

#ifdef D3D9_ALLOW_UNMAPPING
  D3D9MemoryAllocator::D3D9MemoryAllocator() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    m_allocationGranularity = sysInfo.dwAllocationGranularity;
  }

#endif // UNMAP_V1

  D3D9Memory D3D9MemoryAllocator::Alloc(uint32_t Size) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

#ifdef UNMAP_V1
    uint32_t alignedSize = align(Size, 16);
#else
    uint32_t alignedSize = align(Size, CACHE_LINE_SIZE);
#endif
    for (auto& chunk : m_chunks) {
      D3D9Memory memory = chunk->Alloc(alignedSize);
      if (memory) {
        m_usedMemory += memory.GetSize();
        return memory;
      }
    }

#ifdef UNMAP_V1
    alignedSize = align(Size, 4 << 10);
#else
#endif
    uint32_t chunkSize = std::max(D3D9ChunkSize, alignedSize);
    m_allocatedMemory += chunkSize;

    D3D9MemoryChunk* chunk = new D3D9MemoryChunk(this, chunkSize);
    std::unique_ptr<D3D9MemoryChunk> uniqueChunk(chunk);
    D3D9Memory memory = uniqueChunk->Alloc(alignedSize);
    m_usedMemory += memory.GetSize();

    m_chunks.push_back(std::move(uniqueChunk));
    return memory;
  }

  void D3D9MemoryAllocator::FreeChunk(D3D9MemoryChunk *Chunk) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    m_allocatedMemory -= Chunk->Size();

    m_chunks.erase(std::remove_if(m_chunks.begin(), m_chunks.end(), [&](auto& item) {
        return item.get() == Chunk;
    }), m_chunks.end());
  }

  void D3D9MemoryAllocator::NotifyMapped(uint32_t Size) {
#ifdef UNMAP_V1
    std::lock_guard<dxvk::mutex> lock(m_mutex);
#else
#endif
    m_mappedMemory += Size;
  }

  void D3D9MemoryAllocator::NotifyUnmapped(uint32_t Size) {
#ifdef UNMAP_V1
    std::lock_guard<dxvk::mutex> lock(m_mutex);
#else
#endif
    m_mappedMemory -= Size;
  }

  void D3D9MemoryAllocator::NotifyFreed(uint32_t Size) {
#ifdef UNMAP_V1
    std::lock_guard<dxvk::mutex> lock(m_mutex);
#else
#endif
    m_usedMemory -= Size;
  }

  uint32_t D3D9MemoryAllocator::MappedMemory() {
#ifdef UNMAP_V1
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    return m_mappedMemory;
  }

  uint32_t D3D9MemoryAllocator::UsedMemory() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    return m_usedMemory;
  }

  uint32_t D3D9MemoryAllocator::AllocatedMemory() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    return m_allocatedMemory;
  }


  D3D9MemoryChunk::D3D9MemoryChunk(D3D9MemoryAllocator* Allocator, uint32_t Size)
    : m_allocator(Allocator), m_size(Size) {
    m_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, Size, nullptr);
    m_freeRanges.push_back({ 0, Size });
#else
    return m_mappedMemory.load();
  }

  uint32_t D3D9MemoryAllocator::UsedMemory() {
    return m_usedMemory.load();
  }

  uint32_t D3D9MemoryAllocator::AllocatedMemory() {
    return m_allocatedMemory.load();
  }

  D3D9MemoryChunk::D3D9MemoryChunk(D3D9MemoryAllocator* Allocator, uint32_t Size)
    : m_allocator(Allocator), m_size(Size), m_mappingGranularity(m_allocator->MemoryGranularity() * 16) {
    m_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, Size, nullptr);
    m_freeRanges.push_back({ 0, Size });
    m_mappingRanges.resize(((Size + m_mappingGranularity - 1) / m_mappingGranularity));
#endif // UNMAP_V1
  }

  D3D9MemoryChunk::~D3D9MemoryChunk() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

#ifdef UNMAP_V1
    if (m_ptr != nullptr) {
      UnmapViewOfFile(m_ptr);
    }
    CloseHandle(m_mapping);
  }

  void D3D9MemoryChunk::IncMapCounter() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);
    m_mapCounter++;

    if (m_mapCounter == 1) {
      m_allocator->NotifyMapped(m_size);
      m_ptr = MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, m_size);
    }
  }

  void D3D9MemoryChunk::DecMapCounter() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);
    m_mapCounter--;

    if (m_mapCounter == 0) {
      m_allocator->NotifyUnmapped(m_size);
      UnmapViewOfFile(m_ptr);
      m_ptr = nullptr;
#else
    CloseHandle(m_mapping);
  }

  void* D3D9MemoryChunk::Map(D3D9Memory* memory) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    uint32_t alignedOffset = alignDown(memory->GetOffset(), m_mappingGranularity);
    uint32_t alignmentDelta = memory->GetOffset() - alignedOffset;
    uint32_t alignedSize = memory->GetSize() + alignmentDelta;
    if (alignedSize > m_mappingGranularity) {
      // The allocation crosses the boundary of the internal mapping page it's a part of
      // so we map it on it's own.
      alignedOffset = alignDown(memory->GetOffset(), m_allocator->MemoryGranularity());
      alignmentDelta = memory->GetOffset() - alignedOffset;
      alignedSize = memory->GetSize() + alignmentDelta;

      m_allocator->NotifyMapped(alignedSize);
      uint8_t* basePtr = static_cast<uint8_t*>(MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, alignedOffset, alignedSize));
      if (unlikely(basePtr == nullptr)) {
        DWORD error = GetLastError();
        Logger::err(str::format("Mapping non-persisted file failed: ", error, ", Mapped memory: ", m_allocator->MappedMemory()));
        return nullptr;
      }
      return basePtr + alignmentDelta;
    }

    // For small allocations we map the entire mapping page to minimize the overhead from having the align the offset to 65k bytes.
    // This should hopefully also reduce the amount of MapViewOfFile calls we do for tiny allocations.
    auto& mappingRange = m_mappingRanges[memory->GetOffset() /  m_mappingGranularity];
    if (unlikely(mappingRange.refCount == 0)) {
      m_allocator->NotifyMapped(m_mappingGranularity);
      mappingRange.ptr = static_cast<uint8_t*>(MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, alignedOffset, m_mappingGranularity));
      if (unlikely(mappingRange.ptr == nullptr)) {
        DWORD error = GetLastError();
        LPTSTR buffer = nullptr;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), (LPTSTR)&buffer, 0, nullptr);
        Logger::err(str::format("Mapping non-persisted file failed: ", error, ", Mapped memory: ", m_allocator->MappedMemory(), ", Msg: ", buffer));
        if (buffer) {
          LocalFree(buffer);
        }
      }
    }
    mappingRange.refCount++;
    uint8_t* basePtr = static_cast<uint8_t*>(mappingRange.ptr);
    return basePtr + alignmentDelta;
  }

  void D3D9MemoryChunk::Unmap(D3D9Memory* memory) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    uint32_t alignedOffset = alignDown(memory->GetOffset(), m_mappingGranularity);
    uint32_t alignmentDelta = memory->GetOffset() - alignedOffset;
    uint32_t alignedSize = memory->GetSize() + alignmentDelta;
    if (alignedSize > m_mappingGranularity) {
      // Single use mapping
      alignedOffset = alignDown(memory->GetOffset(), m_allocator->MemoryGranularity());
      alignmentDelta = memory->GetOffset() - alignedOffset;
      alignedSize = memory->GetSize() + alignmentDelta;

      uint8_t* basePtr = static_cast<uint8_t*>(memory->Ptr()) - alignmentDelta;
      UnmapViewOfFile(basePtr);
      m_allocator->NotifyUnmapped(alignedSize);
      return;
    }
    auto& mappingRange = m_mappingRanges[memory->GetOffset() /  m_mappingGranularity];
    mappingRange.refCount--;
    if (unlikely(mappingRange.refCount == 0)) {
      UnmapViewOfFile(mappingRange.ptr);
      mappingRange.ptr = nullptr;
      m_allocator->NotifyUnmapped(m_mappingGranularity);
#endif // UNMAP_V1
    }
  }

  D3D9Memory D3D9MemoryChunk::Alloc(uint32_t Size) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    uint32_t offset = 0;
    uint32_t size = 0;

    for (auto range = m_freeRanges.begin(); range != m_freeRanges.end(); range++) {
      if (range->length >= Size) {
        offset = range->offset;
        size = Size;
        range->offset += Size;
        range->length -= Size;
        if (range->length < (4 << 10)) {
          size += range->length;
          m_freeRanges.erase(range);
        }
        break;
      }
    }

    if (size != 0)
#ifdef UNMAP_V1
      return D3D9Memory(this, offset, size);
#else
      return D3D9Memory(this, offset, Size);
#endif

    return {};
  }

  void D3D9MemoryChunk::Free(D3D9Memory *Memory) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    uint32_t offset = Memory->GetOffset();
#ifdef UNMAP_V1
    uint32_t length = Memory->GetSize();
#else
    uint32_t size = Memory->GetSize();
#endif

    auto curr = m_freeRanges.begin();

    // shamelessly stolen from dxvk_memory.cpp
    while (curr != m_freeRanges.end()) {
#ifdef UNMAP_V1
      if (curr->offset == offset + length) {
        length += curr->length;
        curr = m_freeRanges.erase(curr);
      } else if (curr->offset + curr->length == offset) {
        offset -= curr->length;
        length += curr->length;
#else
      if (curr->offset == offset + size) {
        size += curr->length;
        curr = m_freeRanges.erase(curr);
      } else if (curr->offset + curr->length == offset) {
        offset -= curr->length;
        size += curr->length;
#endif
        curr = m_freeRanges.erase(curr);
      } else {
        curr++;
      }
    }

#ifdef UNMAP_V1
    m_freeRanges.push_back({ offset, length });
#else
    m_freeRanges.push_back({ offset, size });
#endif
    m_allocator->NotifyFreed(Memory->GetSize());
  }

  bool D3D9MemoryChunk::IsEmpty() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    return m_freeRanges.size() == 1
        && m_freeRanges[0].length == m_size;
  }

  D3D9MemoryAllocator* D3D9MemoryChunk::Allocator() const {
    return m_allocator;
  }

#ifdef UNMAP_V1
#else
  HANDLE D3D9MemoryChunk::FileHandle() const {
    return m_mapping;
  }

#endif

  D3D9Memory::D3D9Memory(D3D9MemoryChunk* Chunk, size_t Offset, size_t Size)
    : m_chunk(Chunk), m_offset(Offset), m_size(Size) {}

  D3D9Memory::D3D9Memory(D3D9Memory&& other)
    : m_chunk(std::exchange(other.m_chunk, nullptr)),
      m_ptr(std::exchange(other.m_ptr, nullptr)),
      m_offset(std::exchange(other.m_offset, 0)),
      m_size(std::exchange(other.m_size, 0)) {}

  D3D9Memory::~D3D9Memory() {
    this->Free();
  }

  D3D9Memory& D3D9Memory::operator = (D3D9Memory&& other) {
    this->Free();

    m_chunk = std::exchange(other.m_chunk, nullptr);
    m_ptr = std::exchange(other.m_ptr, nullptr);
    m_offset = std::exchange(other.m_offset, 0);
    m_size = std::exchange(other.m_size, 0);
    return *this;
  }

  void D3D9Memory::Free() {
    if (unlikely(m_chunk == nullptr))
      return;

    if (m_ptr != nullptr)
#ifdef UNMAP_V1
      m_chunk->DecMapCounter();
#else
      Unmap();
#endif

    m_chunk->Free(this);
    if (m_chunk->IsEmpty()) {
      D3D9MemoryAllocator* allocator = m_chunk->Allocator();
      allocator->FreeChunk(m_chunk);
    }
    m_chunk = nullptr;
  }

  void D3D9Memory::Map() {
    if (unlikely(m_ptr != nullptr))
      return;

#ifdef UNMAP_V1
    if (unlikely(m_chunk == nullptr)) {
      Logger::warn("Tried to map dead memory");
      return;
    }

    m_chunk->IncMapCounter();
    uint8_t* ptr = reinterpret_cast<uint8_t*>(m_chunk->Ptr());
    m_ptr = ptr + m_offset;
#else
    if (unlikely(m_chunk == nullptr))
      return;

    m_ptr = m_chunk->Map(this);
#endif
  }

  void D3D9Memory::Unmap() {
    if (unlikely(m_ptr == nullptr))
      return;

#ifdef UNMAP_V1
    m_ptr = nullptr;
    m_chunk->DecMapCounter();
  }

  void* D3D9Memory::Ptr() {
    if (unlikely(m_ptr == nullptr)) {
      Logger::warn("Tried accessing unmapped memory.");
      return nullptr;
    }

#else
    m_chunk->Unmap(this);
    m_ptr = nullptr;
  }

  void* D3D9Memory::Ptr() {
#endif
    return m_ptr;
  }

#else

#ifdef UNMAP_V1
    D3D9Memory D3D9MemoryAllocator::Alloc(uint32_t Size) {
      std::lock_guard<dxvk::mutex> lock(m_mutex);

      m_usedMemory += Size;

      return D3D9Memory(this, Size);
    }

    void D3D9MemoryAllocator::NotifyFreed(uint32_t Size) {
      std::lock_guard<dxvk::mutex> lock(m_mutex);

      m_usedMemory -= Size;
    }

    uint32_t D3D9MemoryAllocator::MappedMemory() {
      std::lock_guard<dxvk::mutex> lock(m_mutex);

      return m_usedMemory;
    }

    uint32_t D3D9MemoryAllocator::UsedMemory() {
      std::lock_guard<dxvk::mutex> lock(m_mutex);

      return m_usedMemory;
    }

    uint32_t D3D9MemoryAllocator::AllocatedMemory() {
      std::lock_guard<dxvk::mutex> lock(m_mutex);

      return m_usedMemory;
    }

    D3D9Memory::D3D9Memory(D3D9MemoryAllocator* Allocator, size_t Size)
      : m_allocator(Allocator), m_ptr(std::malloc(Size)), m_size(Size) { }

    D3D9Memory::D3D9Memory(D3D9Memory&& other)
      : m_allocator(std::exchange(other.m_allocator, nullptr)),
        m_ptr(std::exchange(other.m_ptr, nullptr)),
        m_size(std::exchange(other.m_size, 0)) {}

    D3D9Memory::~D3D9Memory() {
      this->Free();
    }

    D3D9Memory& D3D9Memory::operator = (D3D9Memory&& other) {
      this->Free();

      m_allocator = std::exchange(other.m_allocator, nullptr);
      m_ptr = std::exchange(other.m_ptr, nullptr);
      m_size = std::exchange(other.m_size, 0);
      return *this;
    }

    void D3D9Memory::Free() {
      if (!m_ptr)
        return;

      m_allocator->NotifyFreed(m_size);

      std::free(m_ptr);
    }
#else
  D3D9Memory D3D9MemoryAllocator::Alloc(uint32_t Size) {
    D3D9Memory memory(this, Size);
    m_allocatedMemory += Size;
    return memory;
  }

  uint32_t D3D9MemoryAllocator::MappedMemory() {
    return m_allocatedMemory.load();
  }

  uint32_t D3D9MemoryAllocator::UsedMemory() {
    return m_allocatedMemory.load();
  }

  uint32_t D3D9MemoryAllocator::AllocatedMemory() {
    return m_allocatedMemory.load();
  }

  D3D9Memory::D3D9Memory(D3D9MemoryAllocator* pAllocator, size_t Size)
    : m_allocator (pAllocator),
      m_ptr       (malloc(Size)),
      m_size      (Size) {}

  D3D9Memory::D3D9Memory(D3D9Memory&& other)
    : m_allocator(std::exchange(other.m_allocator, nullptr)),
      m_ptr(std::exchange(other.m_ptr, nullptr)),
      m_size(std::exchange(other.m_size, 0)) {}

  D3D9Memory::~D3D9Memory() {
    this->Free();
  }

  D3D9Memory& D3D9Memory::operator = (D3D9Memory&& other) {
    this->Free();

    m_allocator = std::exchange(other.m_allocator, nullptr);
    m_ptr = std::exchange(other.m_ptr, nullptr);
    m_size = std::exchange(other.m_size, 0);
    return *this;
  }

  void D3D9Memory::Free() {
    if (m_ptr == nullptr)
      return;

    free(m_ptr);
    m_ptr = nullptr;
    m_allocator->NotifyFreed(m_size);
  }

#endif // UNMAP_V1

#endif

}
