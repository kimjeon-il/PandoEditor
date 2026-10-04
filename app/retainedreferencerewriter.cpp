#include "retainedreferencerewriter.h"
#include "losslessjson.h"
#include <algorithm>
#include <optional>
#include <set>
namespace retainedrefs {
using namespace pandoeditor;
namespace {
void dependencies(const losslessjson::Value& value,std::vector<ObjectRef>& output) {
    if(value.kind==losslessjson::Value::Object) for(const auto& [key,child]:value.object) {
        if((key=="territorialUnitId"||key=="ownerId")&&child.kind==losslessjson::Value::String&&!child.string.empty()) output.push_back(territorialRef(child.string));
        dependencies(child,output);
    } else if(value.kind==losslessjson::Value::Array) for(const auto& child:value.array)dependencies(child,output);
}
bool replace(losslessjson::Value& value,const ReferenceRewrite& rewrite) {
    if(value.kind!=losslessjson::Value::String||value.string!=rewrite.fromId)return false;
    if(rewrite.operation!=ReferenceRewriteOperation::ReplaceId)return false;value=losslessjson::Value::str(rewrite.toId);return true;
}
bool rewriteArray(losslessjson::Value& root,const ReferenceRewrite& rewrite) {
    if(root.kind!=losslessjson::Value::Array)return false;bool changed=false;
    auto out=root.array;out.clear();for(auto row:root.array) {bool remove=false,ownerMatched=false;if(row.kind==losslessjson::Value::Object) {
        for(auto key:{"territorialUnitId","ownerId"}) {auto it=row.object.find(key);if(it!=row.object.end()) {if(rewrite.operation==ReferenceRewriteOperation::RemoveEntry&&it->second.kind==losslessjson::Value::String&&it->second.string==rewrite.fromId)remove=true;changed=replace(it->second,rewrite)||changed;}}
        auto props=row.object.find("properties");if(props!=row.object.end()&&props->second.kind==losslessjson::Value::Object) {auto owner=props->second.object.find("ownerId");if(owner!=props->second.object.end()) {ownerMatched=owner->second.kind==losslessjson::Value::String&&owner->second.string==rewrite.fromId;if(rewrite.operation==ReferenceRewriteOperation::RemoveEntry&&owner->second.kind==losslessjson::Value::String&&owner->second.string==rewrite.fromId)remove=true;changed=replace(owner->second,rewrite)||changed;}}
        if(ownerMatched&&rewrite.sourcePath=="/genericFeatures"&&rewrite.operation==ReferenceRewriteOperation::ReplaceId&&props!=row.object.end()&&props->second.kind==losslessjson::Value::Object) {
            auto topology=props->second.object.find("topologyGroup");
            if(topology!=props->second.object.end()&&topology->second.kind==losslessjson::Value::String&&topology->second.string=="land:"+rewrite.fromId) {
                topology->second=losslessjson::Value::str("land:"+rewrite.toId);changed=true;
            }
        }
      }if(!remove)out.push_back(std::move(row));else changed=true;}root.array=std::move(out);return changed;
}
std::optional<std::string> rewrittenKey(const std::string& key,const std::string& path,const ReferenceRewrite& rewrite) {
    if(key==rewrite.fromId) return rewrite.toId;
    if(path=="/labelSettings") {
        for(const auto* prefix:{"territorial:entity:","country:","subunit:","region:","territorial:country:","territorial:subunit:","territorial:region:"}) {
            const std::string p(prefix);
            if(key==p+rewrite.fromId) return p+rewrite.toId;
        }
    }
    return std::nullopt;
}
bool rewriteKeys(losslessjson::Value& root,const std::string& path,const ReferenceRewrite& rewrite) {
    if(root.kind!=losslessjson::Value::Object)return false;bool changed=false;
    for(auto it=root.object.begin();it!=root.object.end();) {
        const auto replacement=rewrittenKey(it->first,path,rewrite);
        if(replacement) {
            if(rewrite.operation==ReferenceRewriteOperation::ReplaceId) {auto value=std::move(it->second);it=root.object.erase(it);root.object.emplace(*replacement,std::move(value));changed=true;continue;}
            if(rewrite.operation==ReferenceRewriteOperation::DeleteKey||rewrite.operation==ReferenceRewriteOperation::RemoveEntry){it=root.object.erase(it);changed=true;continue;}
        }
        if(path=="/itemVisibility") changed=rewriteKeys(it->second,path,rewrite)||changed;
        ++it;
    }
    return changed;
}
}
std::vector<ObjectRef> dependenciesFor(const std::string&,const losslessjson::Value& payload) {std::vector<ObjectRef> result;dependencies(payload,result);std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;}
RewriteResult rewrite(const ProjectDocument&,const TerritorialMutationPlan& plan,std::vector<PreservedExtension>& candidate) {
    RewriteResult result;for(auto& extension:candidate) {if(extension.status=="migrationArchive")continue;bool relevant=false;for(const auto& item:plan.rewrites)if(item.sourcePath==extension.jsonPointer){relevant=true;break;}
        if(!relevant)for(const auto& guard:plan.retainedGuards)if(guard.id==extension.id){relevant=true;break;}
        if(!relevant)continue;
        if(extension.jsonPointer!="/distributionEntries"&&extension.jsonPointer!="/itemVisibility"&&extension.jsonPointer!="/labelSettings"&&extension.jsonPointer!="/genericFeatures")return {false,"UNSAFE_RETAINED_REFERENCE: "+extension.jsonPointer,{}};
        try {auto payload=losslessjson::parse(QByteArray::fromStdString(extension.payload));
            const auto expected=extension.jsonPointer=="/distributionEntries"||extension.jsonPointer=="/genericFeatures"?losslessjson::Value::Array:losslessjson::Value::Object;
            if(payload.kind!=expected)return {false,"UNSAFE_RETAINED_REFERENCE: invalid payload",{}};
            if(expected==losslessjson::Value::Array)for(const auto& row:payload.array) {
                if(row.kind!=losslessjson::Value::Object)return {false,"UNSAFE_RETAINED_REFERENCE: invalid entry",{}};
                for(const auto* key:{"ownerId","territorialUnitId"}) {
                    const auto field=row.object.find(key);
                    if(field!=row.object.end()&&field->second.kind!=losslessjson::Value::String)return {false,"UNSAFE_RETAINED_REFERENCE: invalid reference",{}};
                }
                const auto properties=row.object.find("properties");
                if(properties!=row.object.end()) {
                    if(properties->second.kind!=losslessjson::Value::Object)return {false,"UNSAFE_RETAINED_REFERENCE: invalid properties",{}};
                    const auto owner=properties->second.object.find("ownerId");
                    if(owner!=properties->second.object.end()&&owner->second.kind!=losslessjson::Value::String)return {false,"UNSAFE_RETAINED_REFERENCE: invalid owner",{}};
                }
            }
            for(const auto& item:plan.rewrites)if(item.sourcePath==extension.jsonPointer) {
            const bool changed=(extension.jsonPointer=="/distributionEntries"||extension.jsonPointer=="/genericFeatures")?rewriteArray(payload,item):rewriteKeys(payload,extension.jsonPointer,item);
            (void)changed;
        }extension.payload=payload.encode().toStdString();extension.dependencies=dependenciesFor(extension.jsonPointer,payload);result.handledExtensionIds.push_back(extension.id);
        } catch(const std::exception& e) {return {false,e.what(),{}};}
    }return result;
}
}
