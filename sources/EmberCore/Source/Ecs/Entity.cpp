// Ecs/Entity.cpp

#include "Ecs/Entity.h"

namespace Ember
{
    std::string ToString(const Entity& entity)
    {
        return entity.IsValid() ? "Entity(" + std::to_string(entity.Index) + "v" +
                                      std::to_string(entity.Generation) + ")"
                                : std::string("Entity(Null)");
    }
}
