#pragma once
#include <string>
#include <algorithm>
#include <cstdio>

namespace apb {

struct RGB { double r=0,g=0,b=0; };

inline RGB WRITCH_START(){ return {0.660768,0.00396,0.0280088}; }
inline RGB WRITCH_END()  { return {0.2,0.004,0.9}; }
inline RGB SPELL_START() { return {0.08,0.0,0.78}; }
inline RGB SPELL_END()   { return {0.65,0.022353,0.4}; }

inline double lerpD(double a,double b,double t){ return a+(b-a)*t; }
inline RGB lerpRGB(const RGB& a,const RGB& b,double t){
    return {lerpD(a.r,b.r,t),lerpD(a.g,b.g,t),lerpD(a.b,b.b,t)};
}
inline std::string colorTag(const RGB& c,const std::string& text){
    char buf[512];
    snprintf(buf,sizeof(buf),"<Color:R=%.3f G=%.3f B=%.3f>%s<Color:/>",c.r,c.g,c.b,text.c_str());
    return buf;
}
inline std::string gradientText(const std::string& label,const RGB& c1,const RGB& c2){
    if(label.empty()) return {};
    int n=std::max(1,(int)label.size()-1);
    std::string out;
    for(int i=0;i<(int)label.size();++i){
        double t=n==0?0.0:double(i)/n;
        out+=colorTag(lerpRGB(c1,c2,t),std::string(1,label[i]));
    }
    return out;
}
inline std::string tripleGradientText(const std::string& label,const RGB& c1,const RGB& c2,const RGB& c3){
    if(label.empty()) return {};
    int n=std::max(1,(int)label.size()-1);
    std::string out;
    for(int i=0;i<(int)label.size();++i){
        double t=n==0?0.0:double(i)/n;
        RGB c = (t <= 0.5)
            ? lerpRGB(c1,c2,t*2.0)
            : lerpRGB(c2,c3,(t-0.5)*2.0);
        out+=colorTag(c,std::string(1,label[i]));
    }
    return out;
}
inline std::string solidText(const std::string& lbl,const RGB& c){ return colorTag(c,lbl); }

} // namespace apb
