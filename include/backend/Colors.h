#pragma once
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace apb {

// Format a float without trailing zeros: 0.650000 → "0.65", 1.000000 → "1"
inline std::string fmtF(double v){
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    char* dot = std::strchr(buf, '.');
    if(dot){
        char* end = buf + std::strlen(buf) - 1;
        while(end > dot && *end == '0') *end-- = '\0';
        if(*end == '.') *end = '\0';
    }
    return buf;
}

struct RGB { double r=0,g=0,b=0; };

inline RGB WRITCH_START(){ return {0.660768,0.00396,0.0280088}; }
inline RGB WRITCH_END()  { return {0.2,0.004,0.9}; }
inline RGB SPELL_START() { return {0.08,0.0,0.78}; }
inline RGB SPELL_END()   { return {0.65,0.022353,0.4}; }

inline double lerpD(double a,double b,double t){ return a+(b-a)*t; }
inline RGB lerpRGB(const RGB& a,const RGB& b,double t){
    return {lerpD(a.r,b.r,t),lerpD(a.g,b.g,t),lerpD(a.b,b.b,t)};
}
inline std::string openColorTag(const RGB& c){
    return "<Color:R=" + fmtF(c.r) + " G=" + fmtF(c.g) + " B=" + fmtF(c.b) + ">";
}
inline std::string colorTag(const RGB& c,const std::string& text){
    return openColorTag(c) + text + "<Color:/>";
}
inline std::string gradientText(const std::string& label,const RGB& c1,const RGB& c2){
    if(label.empty()) return {};
    int n=std::max(1,(int)label.size()-1);
    std::string out;
    bool open=false;
    for(int i=0;i<(int)label.size();++i){
        if(label[i]==' '){
            if(open){out+="<Color:/>";open=false;}
            out+=label[i];
            continue;
        }
        double t=n==0?0.0:double(i)/n;
        out+=openColorTag(lerpRGB(c1,c2,t));
        out+=label[i];
        open=true;
    }
    if(open) out+="<Color:/>";
    return out;
}
inline std::string tripleGradientText(const std::string& label,const RGB& c1,const RGB& c2,const RGB& c3){
    if(label.empty()) return {};
    int n=std::max(1,(int)label.size()-1);
    std::string out;
    bool open=false;
    for(int i=0;i<(int)label.size();++i){
        if(label[i]==' '){
            if(open){out+="<Color:/>";open=false;}
            out+=label[i];
            continue;
        }
        double t=n==0?0.0:double(i)/n;
        RGB c = (t <= 0.5)
            ? lerpRGB(c1,c2,t*2.0)
            : lerpRGB(c2,c3,(t-0.5)*2.0);
        out+=openColorTag(c);
        out+=label[i];
        open=true;
    }
    if(open) out+="<Color:/>";
    return out;
}
inline std::string solidText(const std::string& lbl,const RGB& c){ return colorTag(c,lbl); }

} // namespace apb
