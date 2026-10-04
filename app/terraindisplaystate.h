#pragma once
#include <QVariantList>
// A partial viewport may reuse the last complete view, never earlier partial views.
class TerrainDisplayState {
public:
    QVariantList publish(QVariantList ready,bool complete) {
        if(complete)lastComplete_=ready;
        else for(const auto& old:lastComplete_)if(!ready.contains(old))ready.push_back(old);
        return ready;
    }
    void reset(){lastComplete_.clear();}
private:
    QVariantList lastComplete_;
};
