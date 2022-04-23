#include "d3d9_hud.h"

namespace dxvk::hud {

  HudSamplerCount::HudSamplerCount(D3D9DeviceEx* device)
    : m_device       (device)
    , m_samplerCount ("0"){

  }


  void HudSamplerCount::update(dxvk::high_resolution_clock::time_point time) {
    m_samplerCount = str::format(m_device->GetSamplerCount());
  }


  HudPos HudSamplerCount::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;

    renderer.drawText(16.0f,
      { position.x, position.y },
      { 0.0f, 1.0f, 0.75f, 1.0f },
      "Samplers:");

    renderer.drawText(16.0f,
      { position.x + 120.0f, position.y },
      { 1.0f, 1.0f, 1.0f, 1.0f },
      m_samplerCount);

    position.y += 8.0f;
    return position;
  }

  HudManagedMemory::HudManagedMemory(D3D9DeviceEx* device)
          : m_device       (device)
          , m_memoryText ("") {}


  void HudManagedMemory::update(dxvk::high_resolution_clock::time_point time) {
    D3D9MemoryAllocator* allocator = m_device->GetAllocator();

    static uint32_t peakMapped = 0u;
    if (allocator->MappedMemory() > peakMapped)
      peakMapped = allocator->MappedMemory();

    if ((::GetAsyncKeyState(VK_NUMPAD0) & 1) != 0)
      peakMapped = 0u;

    m_memoryText = str::format(std::setfill(' '), allocator->AllocatedMemory() >> 20, " MB allocated ",
        std::setw(5),  allocator->UsedMemory() >> 20, " MB used ", std::setw(5), allocator->MappedMemory() >> 20, " MB mapped ", std::setw(5), peakMapped >> 20, " MB peak ");
  }


  HudPos HudManagedMemory::render(
          HudRenderer&      renderer,
          HudPos            position) {
    position.y += 16.0f;

    renderer.drawText(16.0f,
                      { position.x, position.y },
                      { 0.0f, 1.0f, 0.75f, 1.0f },
                      "Managed memory:");

    renderer.drawText(16.0f,
                      { position.x + 200.0f, position.y },
                      { 1.0f, 1.0f, 1.0f, 1.0f },
                      m_memoryText);

    position.y += 8.0f;
    return position;
  }

}