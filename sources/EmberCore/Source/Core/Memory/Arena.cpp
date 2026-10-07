// Core/Memory/Arena.cpp

#include "Core/Memory/Arena.h"

#include <algorithm>
#include <cassert>
#include <new>

namespace Ember
{
    namespace
    {
        constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        constexpr std::size_t DefaultBlockSize = 64 * 1024;
        constexpr std::size_t MaximumBlockSize = 4 * 1024 * 1024;
    }

    Arena::Arena(std::size_t defaultBlockSize)
        : m_DefaultBlockSize(defaultBlockSize > 0 ? defaultBlockSize : DefaultBlockSize)
    {
        AddBlock(m_DefaultBlockSize);
    }

    void Arena::BeginSection(std::string_view name)
    {
        // Sections are nested in time, so starting one retires the previous one.
        EndSection();
        m_Section = Section{name, m_CurrentBlockIndex, m_Offset, m_UsedBytes};
    }

    void Arena::EndSection()
    {
        if (m_HasOpenSection)
        {
            m_CurrentBlockIndex = m_Section.BlockIndex;
            m_Offset = m_Section.Offset;
            m_UsedBytes = m_Section.UsedBytes;
            m_HasOpenSection = false;
        }
    }

    void Arena::Reset()
    {
        m_CurrentBlockIndex = 0;
        m_Offset = 0;
        m_UsedBytes = 0;
        m_Section = Section{};
        m_HasOpenSection = false;
    }

    void Arena::AddBlock(std::size_t minimumSize)
    {
        // Grow geometrically, capped, so that a long-lived arena that keeps
        // outgrowing its blocks does not reallocate on every large request.
        const std::size_t previous = m_Blocks.empty() ? m_DefaultBlockSize : m_Blocks.back().Size;
        std::size_t size = std::max(minimumSize, previous);

        if (size < MaximumBlockSize)
        {
            size = std::min(size * 2, MaximumBlockSize);
        }

        Block block;
        block.Memory = std::unique_ptr<std::byte[]>(new (std::nothrow) std::byte[size]);
        block.Size = block.Memory ? size : 0;

        m_Blocks.push_back(std::move(block));
        m_CurrentBlockIndex = m_Blocks.size() - 1;
        m_Offset = 0;
    }

    std::byte* Arena::GetCurrentBlock() noexcept
    {
        return m_Blocks[m_CurrentBlockIndex].Memory.get();
    }

    void* Arena::Allocate(std::size_t size, std::size_t alignment)
    {
        if (size == 0)
        {
            size = 1;
        }

        alignment = std::max<std::size_t>(alignment, 1);

        // The offset stored in a section is kept aligned, otherwise restoring it
        // would leave later allocations misaligned.
        const std::size_t alignedOffset = AlignUp(m_Offset, alignment);
        Block& current = m_Blocks[m_CurrentBlockIndex];

        if (current.Memory == nullptr || alignedOffset + size > current.Size)
        {
            // Carry the alignment requirement into the new block's start.
            AddBlock(size + alignment);

            const std::size_t blockStart = AlignUp(m_Offset, alignment);
            m_Offset = blockStart + size;
            m_UsedBytes += size;
            return GetCurrentBlock() + blockStart;
        }

        m_Offset = alignedOffset + size;
        m_UsedBytes += size;
        return GetCurrentBlock() + alignedOffset;
    }

    std::size_t Arena::GetCapacityBytes() const noexcept
    {
        std::size_t capacity = 0;
        for (const Block& block : m_Blocks)
        {
            capacity += block.Size;
        }

        return capacity;
    }
}
