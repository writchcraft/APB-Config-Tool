#include "backend/GradientMaker.h"
#include <cstdio>
#include <algorithm>

namespace apb {

std::vector<RGB> writchGradient6(){
    return {{0.200,0.004,0.898},{0.292,0.004,0.724},{0.384,0.004,0.550},
            {0.475,0.004,0.376},{0.567,0.004,0.202},{0.659,0.004,0.027}};
}
std::vector<RGB> spellboundGradient6(){
    return {{0.080,0.000,0.780},{0.138,0.010,0.680},{0.241,0.040,0.811},
            {0.551,0.150,0.845},{0.556,0.082,0.729},{0.650,0.022,0.400}};
}
RGB witchSmoothEnd()   { return {0xa8/255.0,0x01/255.0,0x06/255.0}; }
RGB writchSmoothStart(){ return {0x33/255.0,0x01/255.0,0xe4/255.0}; }
RGB spellboundStart()  { return {0x14/255.0,0x00/255.0,0xc6/255.0}; }
RGB spellboundEnd()    { return {0xa5/255.0,0x05/255.0,0x66/255.0}; }

std::vector<RGB> steppedPresetByIndex(int presetIdx){
    return (presetIdx == 1) ? spellboundGradient6() : writchGradient6();
}

void smoothPresetByIndex(int presetIdx, RGB& start, RGB& end){
    if (presetIdx == 1) {
        start = spellboundStart();
        end = spellboundEnd();
    } else {
        start = writchSmoothStart();
        end = witchSmoothEnd();
    }
}

static std::vector<int> bounce(int m){
    if(m<=1) return {0};
    std::vector<int> p;
    for(int i=0;i<m;++i) p.push_back(i);
    for(int i=m-2;i>=1;--i) p.push_back(i);
    return p;
}

std::string hardGradientString(const std::string& text,const std::vector<RGB>& pal,bool skip){
    if(pal.empty()) return text;
    auto pat=bounce((int)pal.size());
    int period=(int)pat.size(),step=0;
    std::string out;
    bool open=false;
    for(char ch:text){
        if(skip&&ch==' '){
            if(open){out+="<Color:/>";open=false;}
            out+=ch;
            continue;
        }
        const RGB& c=pal[pat[step%period]];
        out+=openColorTag(c); out+=ch; open=true; ++step;
    }
    if(open) out+="<Color:/>";
    return out;
}

std::string smoothGradientString(const std::string& text,const RGB& s,const RGB& e,bool skip){
    std::vector<int> idx;
    for(int i=0;i<(int)text.size();++i) if(!(skip&&text[i]==' ')) idx.push_back(i);
    int N=(int)idx.size(); if(!N) return text;
    std::string out; int k=0; bool open=false;
    for(char ch:text){
        if(skip&&ch==' '){
            if(open){out+="<Color:/>";open=false;}
            out+=ch;
            continue;
        }
        double t=(N==1)?0.0:double(k)/(N-1);
        RGB c=lerpRGB(s,e,t);
        out+=openColorTag(c); out+=ch; open=true; ++k;
    }
    if(open) out+="<Color:/>";
    return out;
}

std::string tripleGradientString(const std::string& text,const RGB& a,const RGB& b,const RGB& c,bool skip){
    std::vector<int> idx;
    for(int i=0;i<(int)text.size();++i) if(!(skip&&text[i]==' ')) idx.push_back(i);
    int N=(int)idx.size(); if(!N) return text;
    std::string out; int k=0; bool open=false;
    for(char ch:text){
        if(skip&&ch==' '){
            if(open){out+="<Color:/>";open=false;}
            out+=ch;
            continue;
        }
        double t=(N==1)?0.0:double(k)/(N-1);
        RGB col = (t <= 0.5)
            ? lerpRGB(a,b,t*2.0)
            : lerpRGB(b,c,(t-0.5)*2.0);
        out+=openColorTag(col); out+=ch; open=true; ++k;
    }
    if(open) out+="<Color:/>";
    return out;
}
} // namespace apb
