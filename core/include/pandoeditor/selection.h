#pragma once
#include <pandoeditor/document.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pandoeditor {
// Session state only: this class cannot access a Project or a ChangeSet.
// Reference: world-map 58e4087, object-selection-controller.js.
class SelectionState {
public:
    const std::vector<ObjectRef>& items() const noexcept { return items_; }
    const std::optional<ObjectRef>& primary() const noexcept { return primary_; }
    std::uint64_t revision() const noexcept { return revision_; }
    bool has(const ObjectRef& ref) const { return contains(items_,ref); }
    std::optional<ObjectRef> rangeAnchor(const std::string& scope) const {
        const auto it=anchors_.find(scope);
        return it==anchors_.end()?std::optional<ObjectRef>{}:it->second;
    }
    bool replace(const ObjectRef& ref,const std::string& scope={}) {
        if(!valid(ref)) return false;
        if(!scope.empty()) anchors_[scope]=ref;
        return apply({ref},ref);
    }
    bool toggle(const ObjectRef& ref,const std::string& scope={}) {
        if(!valid(ref)) return false;
        auto next=items_; auto primary=primary_;
        const auto it=std::find(next.begin(),next.end(),ref);
        if(it==next.end()) { next.push_back(ref); primary=ref; }
        else {
            next.erase(it);
            if(primary && *primary==ref) primary=last(next);
        }
        if(!scope.empty()) anchors_[scope]=ref;
        return apply(std::move(next),std::move(primary));
    }
    bool selectRange(const ObjectRef& ref,const std::vector<ObjectRef>& ordered,
                     const std::string& scope="default",bool additive=false) {
        if(!valid(ref)) return false;
        std::vector<ObjectRef> values;
        for(const auto& value:ordered) if(valid(value)) values.push_back(value);
        const auto target=std::find(values.begin(),values.end(),ref);
        if(target==values.end()) return false;
        const auto anchor=rangeAnchor(scope);
        const auto start=anchor?std::find(values.begin(),values.end(),*anchor):values.end();
        auto next=additive?items_:std::vector<ObjectRef>{};
        if(start==values.end()) append(next,ref);
        else {
            const auto low=std::min(start,target), high=std::max(start,target);
            for(auto it=low;it!=high+1;++it) append(next,*it);
        }
        anchors_[scope]=ref;
        return apply(std::move(next),ref);
    }
    bool setMany(const std::vector<ObjectRef>& refs,
                 std::optional<ObjectRef> requestedPrimary={},const std::string& scope={}) {
        std::vector<ObjectRef> next;
        std::optional<ObjectRef> fallback;
        for(const auto& ref:refs) if(valid(ref)) { append(next,ref); fallback=ref; }
        // Web fallback is the LAST INPUT, not the last unique insertion.
        const auto primary=requestedPrimary && contains(next,*requestedPrimary)?requestedPrimary:fallback;
        if(!scope.empty() && primary) anchors_[scope]=*primary;
        return apply(std::move(next),primary);
    }
    bool remove(const ObjectRef& ref) {
        auto next=items_;
        const auto it=std::find(next.begin(),next.end(),ref);
        if(it==next.end()) return false;
        next.erase(it);
        auto primary=primary_;
        if(primary && *primary==ref) primary=last(next);
        return apply(std::move(next),std::move(primary));
    }
    bool prune(const std::function<bool(const ObjectRef&)>& exists) {
        std::vector<ObjectRef> next;
        for(const auto& ref:items_) if(exists(ref)) next.push_back(ref);
        auto primary=primary_;
        if(primary && !contains(next,*primary)) primary=last(next);
        // The web reducer deliberately retains scope anchors on prune.
        return apply(std::move(next),std::move(primary));
    }
    bool clear() {
        if(items_.empty() && !primary_) return false;
        anchors_.clear();
        return apply({},{});
    }
    void reset() { items_.clear(); primary_.reset(); anchors_.clear(); revision_=0; }
private:
    static bool valid(const ObjectRef& ref) {
        static const std::set<std::string> domains={"territorial","distributionLayer","distributionEntry","generic","hydro","label","userLayer"};
        return !ref.id.empty() && domains.count(ref.domain)!=0;
    }
    static bool contains(const std::vector<ObjectRef>& refs,const ObjectRef& ref) {
        return std::find(refs.begin(),refs.end(),ref)!=refs.end();
    }
    static void append(std::vector<ObjectRef>& refs,const ObjectRef& ref) {
        if(!contains(refs,ref)) refs.push_back(ref);
    }
    static std::optional<ObjectRef> last(const std::vector<ObjectRef>& refs) {
        return refs.empty()?std::optional<ObjectRef>{}:refs.back();
    }
    bool apply(std::vector<ObjectRef> refs,std::optional<ObjectRef> primary) {
        if(items_==refs && primary_==primary) return false;
        items_=std::move(refs); primary_=std::move(primary); ++revision_; return true;
    }
    std::vector<ObjectRef> items_;
    std::optional<ObjectRef> primary_;
    std::map<std::string,ObjectRef> anchors_;
    std::uint64_t revision_=0;
};
} // namespace pandoeditor
