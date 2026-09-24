#pragma once
#include <pandoeditor/territorialmutation.h>
#include <string>
#include <vector>
namespace losslessjson { struct Value; }
namespace retainedrefs {
struct RewriteResult { bool ok=true; std::string detail; std::vector<std::string> handledExtensionIds; };
RewriteResult rewrite(const pandoeditor::ProjectDocument&,const pandoeditor::TerritorialMutationPlan&,std::vector<pandoeditor::PreservedExtension>&);
std::vector<pandoeditor::ObjectRef> dependenciesFor(const std::string&,const losslessjson::Value&);
}
