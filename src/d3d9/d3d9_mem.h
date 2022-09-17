
#pragma once

#include "../util/thread.h"
#define UNMAP_V1

#if defined(_WIN32) && !defined(_WIN64)
  #define D3D9_ALLOW_UNMAPPING
#endif

#ifdef D3D9_ALLOW_UNMAPPING
  #define WIN32_LEAN_AND_MEAN
  #include <winbase.h>
#endif

#ifndef UNMAP_V1
#include <vector>
#endif

namespace dxvk {

  class D3D9MemoryAllocator;
  class D3D9Memory;

#ifdef D3D9_ALLOW_UNMAPPING

  class D3D9MemoryChunk;

#ifdef UNMAP_V1
  constexpr uint32_t D3D9ChunkSize = 16 << 20;
#else
  constexpr uint32_t D3D9ChunkSize = 64 << 20;
#endif


  struct D3D9MemoryRange {
    uint32_t offset;
    uint32_t length;
  };

#ifndef UNMAP_V1
  struct D3D9MappingRange
  {
    uint32_t refCount = 0;
    void* ptr = nullptr;
  };
#endif

  class D3D9MemoryChunk {
    friend D3D9MemoryAllocator;

    public:
      ~D3D9MemoryChunk();

      D3D9MemoryChunk             (const D3D9MemoryChunk&) = delete;
      D3D9MemoryChunk& operator = (const D3D9MemoryChunk&) = delete;

      D3D9MemoryChunk             (D3D9MemoryChunk&& other) = delete;
      D3D9MemoryChunk& operator = (D3D9MemoryChunk&& other) = delete;
#ifdef UNMAP_V1
      void IncMapCounter();
      void DecMapCounter();
      void* Ptr() const { return m_ptr; }
#endif
      D3D9Memory Alloc(uint32_t Size);
      void Free(D3D9Memory* Memory);
      bool IsEmpty();
      uint32_t Size() const { return m_size; }
      D3D9MemoryAllocator* Allocator() const;
#ifndef UNMAP_V1
      HANDLE FileHandle() const;
      void* Map(D3D9Memory* memory);
      void Unmap(D3D9Memory* memory);
#endif
    private:
      D3D9MemoryChunk(D3D9MemoryAllocator* Allocator, uint32_t Size);

      dxvk::mutex m_mutex;
      D3D9MemoryAllocator* m_allocator;
#ifdef UNMAP_V1
      uint32_t m_mapCounter = 0;
#endif
      HANDLE m_mapping;
#ifdef UNMAP_V1
      void* m_ptr;
#endif
      uint32_t m_size;
#ifndef UNMAP_V1
      uint32_t m_mappingGranularity;
#endif
      std::vector<D3D9MemoryRange> m_freeRanges;
#ifndef UNMAP_V1
      std::vector<D3D9MappingRange> m_mappingRanges;
#endif

  };

  class D3D9Memory {
    friend D3D9MemoryChunk;

    public:
      D3D9Memory() {}
      ~D3D9Memory();

      D3D9Memory             (const D3D9Memory&) = delete;
      D3D9Memory& operator = (const D3D9Memory&) = delete;

      D3D9Memory             (D3D9Memory&& other);
      D3D9Memory& operator = (D3D9Memory&& other);

      operator bool() const { return m_chunk != nullptr; }

      void Map();
      void Unmap();
      void* Ptr();
      D3D9MemoryChunk* GetChunk() const { return m_chunk; }
      size_t GetOffset() const { return m_offset; }
      size_t GetSize() const { return m_size; }

    private:
      D3D9Memory(D3D9MemoryChunk* Chunk, size_t Offset, size_t Size);
      void Free();

      D3D9MemoryChunk* m_chunk = nullptr;
      void* m_ptr              = nullptr;
      size_t m_offset          = 0;
      size_t m_size            = 0;
  };

  class D3D9MemoryAllocator {
    friend D3D9MemoryChunk;

    public:
#ifdef UNMAP_V1
      D3D9MemoryAllocator() = default;
#else
      ~D3D9MemoryAllocator();
#endif
      D3D9Memory Alloc(uint32_t Size);
      void FreeChunk(D3D9MemoryChunk* Chunk);
      void NotifyMapped(uint32_t Size);
      void NotifyUnmapped(uint32_t Size);
      void NotifyFreed(uint32_t Size);
      uint32_t MappedMemory();
      uint32_t UsedMemory();
      uint32_t AllocatedMemory();

    private:
      dxvk::mutex m_mutex;
      std::vector<std::unique_ptr<D3D9MemoryChunk>> m_chunks;
#ifdef UNMAP_V1
      size_t m_mappedMemory = 0;
      size_t m_allocatedMemory = 0;
      size_t m_usedMemory = 0;
#else
      std::atomic<size_t> m_mappedMemory = 0;
      std::atomic<size_t> m_allocatedMemory = 0;
      std::atomic<size_t> m_usedMemory = 0;
      uint32_t m_allocationGranularity;
#endif
  };

#else // Don't care about this branch.  Copy TODO_MERGE: master
    class D3D9Memory {
#ifndef UNMAP_V1
      friend D3D9MemoryAllocator;
#endif
    public:
      D3D9Memory(){};
      D3D9Memory(D3D9MemoryAllocator* Allocator, size_t Size);
      ~D3D9Memory();

      D3D9Memory             (const D3D9Memory&) = delete;
      D3D9Memory& operator = (const D3D9Memory&) = delete;

      D3D9Memory             (D3D9Memory&& other);
      D3D9Memory& operator = (D3D9Memory&& other);

      operator bool() const { return m_ptr != nullptr; }

      void Map(){}
      void Unmap(){}
      void* Ptr() { return m_ptr; }

    private:
      void Free();

      D3D9MemoryAllocator* m_allocator = nullptr;
      void* m_ptr              = nullptr;
      size_t m_offset          = 0;
      size_t m_size            = 0;
    };

    class D3D9MemoryAllocator {

    public:
      D3D9MemoryAllocator() = default;
      ~D3D9MemoryAllocator() = default;
      D3D9Memory Alloc(uint32_t Size);
      void NotifyFreed(uint32_t Size);
      uint32_t MappedMemory();
      uint32_t UsedMemory();
      uint32_t AllocatedMemory();

    private:
      dxvk::mutex m_mutex;
      size_t m_usedMemory = 0;
    };

#endif

}
