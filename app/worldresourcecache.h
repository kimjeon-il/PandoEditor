#pragma once
#include <pandoeditor/map/resourcecachepolicy.h>
#include <pandoeditor/map/renderscene.h>
#include <array>

enum class WorldDetail : std::size_t { Preview=0, Canonical=1 };

// Render-only immutable resources. This class cannot mutate a Project or undo stack.
class WorldResourceCache {
public:
    pandoeditor::ResourceRequestToken beginRequest(WorldDetail);
    bool admit(WorldDetail,std::shared_ptr<const WorldBaseFrame>,pandoeditor::ResourceRequestToken);
    void fail(pandoeditor::ResourceRequestToken token){policy_.failRequest(token);}
    std::shared_ptr<const WorldBaseFrame> get(WorldDetail);
    void protect(WorldDetail,pandoeditor::ResourceProtection,bool);
    void setBudget(std::size_t bytes);
    void useCompatibilityBudget();
    void reset();
    bool compatibilityBudget() const noexcept {return !override_;}
    pandoeditor::ResourceCacheSnapshot snapshot() const {return policy_.snapshot();}
    static std::size_t frameBytes(const WorldBaseFrame&);
private:
    static std::size_t index(WorldDetail);
    void trim();
    void updateBudget();
    std::array<std::shared_ptr<const WorldBaseFrame>,2> frames_;
    std::array<std::size_t,2> workingSetBytes_{};
    std::array<std::array<bool,5>,2> protections_{};
    std::array<pandoeditor::ResourceRequestToken,2> requests_{};
    pandoeditor::ResourceCachePolicy<WorldDetail> policy_;
    std::optional<std::size_t> override_;
};
