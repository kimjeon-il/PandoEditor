#include "worldresourcecache.h"
#include <stdexcept>
#include <iostream>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<const WorldBaseFrame> frame(bool preview){auto mesh=std::make_shared<CountryBaseMesh>();mesh->preview=preview;mesh->positionsMicrodegrees={1,2,3,4};auto f=std::make_shared<WorldBaseFrame>();f->mesh=mesh;return f;}
int main(){
    WorldResourceCache cache;
    auto a=cache.beginRequest(WorldDetail::Preview);require(cache.admit(WorldDetail::Preview,frame(true),a),"admit preview");
    auto b=cache.beginRequest(WorldDetail::Canonical);require(cache.admit(WorldDetail::Canonical,frame(false),b),"admit precise");
    require(cache.snapshot().residentCount==2,"compatibility retains both");
    auto held=cache.get(WorldDetail::Preview);cache.protect(WorldDetail::Preview,pandoeditor::ResourceProtection::Visible,true);
    cache.setBudget(0);require(cache.get(WorldDetail::Preview)==held,"visible survives");require(!cache.get(WorldDetail::Canonical),"inactive evicted");
    require(held->mesh->positionsMicrodegrees.size()==4,"retained source valid");
    auto stale=cache.beginRequest(WorldDetail::Canonical);cache.reset();auto fresh=cache.beginRequest(WorldDetail::Canonical);
    require(!cache.admit(WorldDetail::Canonical,frame(false),stale),"old project rejected");require(cache.snapshot().pendingCount==1,"new request preserved");require(cache.snapshot().staleCompletionCount==1,"stale completion counted");
    cache.protect(WorldDetail::Canonical,pandoeditor::ResourceProtection::Editing,true);
    require(cache.admit(WorldDetail::Canonical,frame(false),fresh),"fresh admitted");require(cache.get(WorldDetail::Canonical)!=nullptr,"edit survives zero budget");
    require(!cache.admit(WorldDetail::Canonical,frame(false),fresh),"duplicate complete rejected");
    cache.protect(WorldDetail::Canonical,pandoeditor::ResourceProtection::Editing,false);require(!cache.get(WorldDetail::Canonical),"unpin evicts");
    std::cout<<"world resource cache contract passed\n";
}
