#pragma once

#include <any>
#include <memory>

namespace qcp {

/// Extract an L1 resampler cache from a pipeline's std::any cache slot.
/// Moves the cache into \a dest, clears the pipeline slot, and sets \a l2Dirty.
template<typename CacheType>
void extractL1Cache(std::any& pipelineCache,
                    std::shared_ptr<CacheType>& dest,
                    bool& l2Dirty)
{
    auto* c = std::any_cast<CacheType>(&pipelineCache);
    if (c && c->sourceSize > 0)
    {
        dest = std::make_shared<CacheType>(std::move(*c));
        pipelineCache = std::any{};
        l2Dirty = true;
    }
}

} // namespace qcp
