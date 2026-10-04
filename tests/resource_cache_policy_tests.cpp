#include <pandoeditor/map/resourcecachepolicy.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace pandoeditor;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
using Policy=ResourceCachePolicy<std::string>;
void zeroBudget(){Policy p(0);p.admit("a",10);p.admit("b",20);p.setProtection("a",ResourceProtection::Visible,true);auto v=p.trim();require(v==std::vector<std::string>{"b"},"zero budget evicts only unprotected");auto s=p.snapshot();require(s.residentBytes==10&&s.protectedOverBudgetBytes==10,"protected overage");p.setProtection("a",ResourceProtection::Visible,false);require(p.trim()==std::vector<std::string>{"a"},"unprotected trim");}
void lru(){Policy p(20);p.admit("a",10);p.admit("b",10);p.touch("a");p.admit("c",10);require(p.trim()==std::vector<std::string>{"b"},"oldest used victim");require(p.snapshot().evictionBytes==10,"eviction bytes");}
void pins(){Policy p(0);p.admit("a",10);auto a=p.pin("a",ResourceProtection::Editing);auto b=p.pin("a",ResourceProtection::Editing);p.setProtection("a",ResourceProtection::Selected,true);p.unpin(a);p.unpin(a);require(p.trim().empty(),"duplicate unpin preserves pin");p.unpin(b);require(p.trim().empty(),"selection independent");p.setProtection("a",ResourceProtection::Selected,false);require(p.trim().size()==1,"last protection release");}
void staleLease(){Policy p(0);p.admit("a",10);auto a=p.pin("a",ResourceProtection::Editing);p.resetScope();p.admit("a",20);auto b=p.pin("a",ResourceProtection::Editing);p.unpin(a);require(p.trim().empty(),"old lease cannot unpin new scope");p.unpin(b);require(p.trim().size()==1,"new lease release");}
void requests(){Policy p(100);auto a=p.beginRequest("a",12);auto dup=p.beginRequest("a",12);require(a.sequence==dup.sequence,"same key pending dedup");require(p.snapshot().pendingCount==1&&p.snapshot().pendingEstimatedBytes==12,"pending accounting");require(p.completeRequest(a),"completion once");require(!p.completeRequest(dup),"duplicate completion rejected");auto b=p.beginRequest("b",std::nullopt);require(p.snapshot().pendingUnknownCount==1,"unknown estimate");p.failRequest(b);require(p.snapshot().pendingCount==0&&p.snapshot().failureCount==1,"failed cleanup");auto old=p.beginRequest("a",5);p.resetScope();auto fresh=p.beginRequest("a",5);require(!p.completeRequest(old),"old scope rejected");require(p.snapshot().pendingCount==1,"old completion leaves fresh pending");require(p.completeRequest(fresh),"fresh completion accepted");}
void overflow(){Policy p(std::numeric_limits<std::size_t>::max());require(p.admit("a",10),"admit");require(!p.admit("b",std::numeric_limits<std::size_t>::max()),"overflow rejected");require(p.snapshot().residentCount==1&&p.snapshot().residentBytes==10,"overflow preserves entry");require(p.admit("a",20),"replace");require(p.snapshot().residentBytes==20,"replacement accounting");}
void zeroByteEntries(){Policy p(0);p.admit("empty",0);require(p.trim()==std::vector<std::string>{"empty"},"zero cap means no unprotected entries, including empty payloads");}
struct ThrowKey {
    int value=0;
    inline static int copiesLeft=-1;
    explicit ThrowKey(int v):value(v){}
    ThrowKey(const ThrowKey& other):value(other.value){if(copiesLeft==0)throw std::bad_alloc();if(copiesLeft>0)--copiesLeft;}
    ThrowKey& operator=(const ThrowKey&)=default;
    bool operator<(const ThrowKey& other)const{return value<other.value;}
};
void transactionalTrim(){ResourceCachePolicy<ThrowKey> p(0);p.admit(ThrowKey(1),10);p.admit(ThrowKey(2),20);ThrowKey::copiesLeft=1;bool failed=false;try{p.trim();}catch(const std::bad_alloc&){failed=true;}ThrowKey::copiesLeft=-1;require(failed,"injected allocation failure reached");require(p.snapshot().residentBytes==30&&p.snapshot().residentCount==2,"failed trim leaves metadata transactional");require(p.trim().size()==2,"retry returns every owner victim");}
int main(){transactionalTrim();zeroByteEntries();zeroBudget();lru();pins();staleLease();requests();overflow();std::cout<<"8 resource policy tests passed\n";}
