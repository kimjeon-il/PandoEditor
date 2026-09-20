#pragma once
#include <pandoeditor/project.h>
#include <variant>

namespace pandoeditor {
struct SetPresentationVisibility { std::string key; bool visible; };
struct SetScopedVisibility { std::string group; std::vector<ObjectRef> targets; bool visible; };
struct SetBatchVisibility { std::vector<ObjectRef> targets; std::optional<bool> visible; };
struct PatchGroupPresentation { std::string group; PresentationStyle patch; };
using PresentationAction=std::variant<SetPresentationVisibility,SetScopedVisibility,SetBatchVisibility,PatchGroupPresentation>;
enum class PresentationResult { Applied, NoOp, InvalidArguments, Failed };
class PresentationCommandProcessor {
public:
    static PresentationResult apply(Project&,const PresentationAction&) noexcept;
};
}
