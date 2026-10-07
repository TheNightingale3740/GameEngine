// Ecs/Entity.h
//
// Entity handles.
//
// An entity is nothing but a 32-bit handle into the world's storage. Handles
// carry a generation counter so that a handle to a destroyed entity can be
// detected as stale rather than silently aliasing whichever entity later reuses
// that slot. This matters most in scripting, where a script may hold onto a
// handle across a frame in which the entity was destroyed.

#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace Ember
{
    using EntityIndex = std::uint32_t;
    using EntityGeneration = std::uint32_t;

    /// A generational entity handle.
    struct Entity
    {
        static constexpr EntityIndex InvalidIndex = std::numeric_limits<EntityIndex>::max();

        constexpr Entity() noexcept = default;

        constexpr Entity(EntityIndex index, EntityGeneration generation) noexcept
            : Index(index)
            , Generation(generation)
        {
        }

        /// The handle of an entity that does not exist.
        static constexpr Entity Null() noexcept { return Entity(InvalidIndex, 0); }

        /// True when this handle could refer to a live entity.
        [[nodiscard]] constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }

        /// Two handles are equal only when both the slot and generation match.
        [[nodiscard]] constexpr bool operator==(const Entity& other) const noexcept
        {
            return Index == other.Index && Generation == other.Generation;
        }

        [[nodiscard]] constexpr bool operator!=(const Entity& other) const noexcept { return !(*this == other); }

        /// Orders handles for use in sorted containers.
        [[nodiscard]] constexpr bool operator<(const Entity& other) const noexcept
        {
            if (Index != other.Index)
            {
                return Index < other.Index;
            }

            return Generation < other.Generation;
        }

        EntityIndex Index = InvalidIndex;
        EntityGeneration Generation = 0;
    };

    /// Human-readable form of a handle, for logs and serialisation.
    [[nodiscard]] std::string ToString(const Entity& entity);
}
