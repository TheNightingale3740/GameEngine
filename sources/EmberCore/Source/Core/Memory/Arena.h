// Core/Memory/Arena.h
//
// Bump allocators used for data that is created and released as a unit.
//
// The engine uses arenas for per-frame scratch memory and for the transient
// strings/tables a script frame allocates, so that a frame does not hit the
// general-purpose allocator at all. Arenas are not thread safe; each thread
// that uses one owns it.

#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace Ember
{
    /// A linear bump allocator with named, individually resettable sections.
    ///
    /// Sections exist so that scratch memory with different lifetimes (per
    /// frame, per system) can be released independently without discarding the
    /// whole arena.
    class Arena
    {
    public:
        /// Creates an arena whose first block is `defaultBlockSize` bytes.
        explicit Arena(std::size_t defaultBlockSize = 64 * 1024);

        Arena(const Arena&) = delete;
        Arena& operator=(const Arena&) = delete;
        Arena(Arena&&) noexcept = default;
        Arena& operator=(Arena&&) noexcept = default;

        /// Begins a new named section. Allocations go to the newest section.
        ///
        /// Creating a section releases the memory of the section created before
        /// it, because sections are strictly nested in time.
        void BeginSection(std::string_view name);

        /// Releases every allocation made since the last `BeginSection`, or all
        /// allocations if no section was ever begun.
        void EndSection();

        /// Releases all memory. The arena is reusable afterwards.
        void Reset();

        /// Aligns the write cursor and returns a pointer to `size` uninitialised bytes.
        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

        /// Allocates and constructs a `T`. Destruction is not tracked; use
        /// trivially destructible types, or place non-trivial objects in a section
        /// that outlives them.
        template <typename T, typename... Args>
        [[nodiscard]] T* Construct(Args&&... args)
        {
            void* memory = Allocate(sizeof(T), alignof(T));
            return memory == nullptr ? nullptr : new (memory) T(std::forward<Args>(args)...);
        }

        /// Total bytes currently handed out and not yet released.
        [[nodiscard]] std::size_t GetUsedBytes() const noexcept { return m_UsedBytes; }

        /// Total bytes reserved by the arena across all its blocks.
        [[nodiscard]] std::size_t GetCapacityBytes() const noexcept;

        /// Number of blocks the arena has allocated from the system.
        [[nodiscard]] std::size_t GetBlockCount() const noexcept { return m_Blocks.size(); }

    private:
        struct Block
        {
            std::unique_ptr<std::byte[]> Memory;
            std::size_t Size = 0;
        };

        struct Section
        {
            std::string_view Name;
            std::size_t BlockIndex = 0;
            std::size_t Offset = 0;
            std::size_t UsedBytes = 0;
        };

        void AddBlock(std::size_t minimumSize);
        [[nodiscard]] std::byte* GetCurrentBlock() noexcept;

        std::vector<Block> m_Blocks;
        Section m_Section{};
        bool m_HasOpenSection = false;
        std::size_t m_CurrentBlockIndex = 0;
        std::size_t m_Offset = 0;
        std::size_t m_UsedBytes = 0;
        std::size_t m_DefaultBlockSize = 0;
    };
}
