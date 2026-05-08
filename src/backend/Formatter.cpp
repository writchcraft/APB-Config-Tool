#include "backend/Formatter.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace apb {

// U+21B5 ↵ encoded as UTF-8
static const std::string SEP = "\xe2\x86\xb5";

static std::string fmtF(double x,const char* unit){
    char buf[64]; snprintf(buf,sizeof(buf),"%.2f",x);
    std::string s=buf;
    while(s.size()>1&&s.back()=='0')s.pop_back();
    if(s.back()=='.')s.pop_back();
    return s+unit;
}
static std::string fmtSec(double x){return fmtF(x," sec");}
static std::string fmtM  (double x){return fmtF(x," m");}
static std::string fmtMps(double x){return fmtF(x," m/s");}
static std::string fmtCm (double x){return fmtF(x," cm");}
static std::string fmtNum(double x){return fmtF(x,"");}
static std::string istr  (double x){return std::to_string(int(x));}
static std::string pfx   (const std::string& k){return "WeaponItemTypes_"+k+"_Description=";}

static std::string join(const std::vector<std::string>& v,const std::string& sep){
    std::string o; for(size_t i=0;i<v.size();++i){if(i)o+=sep;o+=v[i];}return o;
}

// ── Plain formatters ────────────────────────────────────────────────────────
static std::string defaultFmt(const std::string& k,const Stats& s){
    return pfx(k)+join({
        "Time to Kill: "+fmtSec(s.ttk),
        "Shots to Kill: "+istr(s.stk),
        "Health Damage: "+istr(s.healthDamage),
        "Stamina Damage: "+istr(s.staminaDamage),
        "Hard Damage: "+fmtNum(s.hardDamage),
        "Effective Range: "+fmtM(s.rampDistanceInM),
        "Fire Interval: "+fmtSec(s.fireInterval),
        "Reload Time: "+fmtSec(s.reloadTime),
        "Equip Time: "+fmtSec(s.equipTime)},SEP)+"\n";
}
static std::string shotgunFmt(const std::string& k,const Stats& s){
    int p=std::max(1,s.nPellets); std::string xs=" x "+std::to_string(p);
    return pfx(k)+join({
        "Time to Kill: "+fmtSec(s.ttk),
        "Shots to Kill: "+istr(s.stk),
        "Health Damage: "+istr(s.healthDamage)+xs,
        "Stamina Damage: "+istr(s.staminaDamage)+xs,
        "Hard Damage: "+fmtNum(s.hardDamage)+xs,
        "Effective Range: "+fmtM(s.rampDistanceInM),
        "Fire Interval: "+fmtSec(s.fireInterval),
        "Reload Time: "+fmtSec(s.reloadTime),
        "Equip Time: "+fmtSec(s.equipTime)},SEP)+"\n";
}
static std::string ltlFmt(const std::string& k,const Stats& s){
    return pfx(k)+join({
        "Time to Stun: "+fmtSec(s.tts),
        "Shots to Stun: "+istr(s.sts),
        "Health Damage: "+istr(s.healthDamage),
        "Stamina Damage: "+istr(s.staminaDamage),
        "Hard Damage: "+fmtNum(s.hardDamage),
        "Effective Range: "+fmtM(s.ltlRange),
        "Fire Interval: "+fmtSec(s.fireInterval),
        "Reload Time: "+fmtSec(s.reloadTime),
        "Equip Time: "+fmtSec(s.equipTime)},SEP)+"\n";
}
static std::string glFmt(const std::string& k,const Stats& s){
    return pfx(k)+join({
        "Time To Kill: "+fmtSec(s.explosiveTtk),
        "Shots to Kill: "+istr(s.explosiveStk),
        "Max Health Damage: "+istr(s.explosiveMaxHealthDamage),
        "Max Stamina Damage: "+istr(s.explosiveStaminaDamage),
        "Max Hard Damage: "+fmtNum(s.explosiveHardDamage),
        "Wind Up Time: "+fmtSec(s.windUpTime),
        "Fire Interval: "+fmtSec(s.fireInterval),
        "Reload Time: "+fmtSec(s.reloadTime),
        "Equip Time: "+fmtSec(s.equipTime)},SEP)+"\n";
}
static std::string glLtlFmt(const std::string& k,const Stats& s){
    return pfx(k)+join({
        "Time to Stun: "+fmtSec(s.explosiveTts),
        "Shots to Stun: "+istr(s.explosiveSts),
        "Max Health Damage: "+istr(s.explosiveMaxHealthDamage),
        "Max Stamina Damage: "+istr(s.explosiveStaminaDamage),
        "Max Hard Damage: "+fmtNum(s.explosiveHardDamage),
        "Wind Up Time: "+fmtSec(s.windUpTime),
        "Fire Interval: "+fmtSec(s.fireInterval),
        "Reload Time: "+fmtSec(s.reloadTime),
        "Equip Time: "+fmtSec(s.equipTime)},SEP)+"\n";
}
static std::string rocketFmt(const std::string& k,const Stats& s){
    return pfx(k)+join({
        "Time To Kill: "+fmtSec(s.explosiveTtk),
        "Shots to Kill: "+istr(s.explosiveStk),
        "Max Health Damage: "+istr(s.explosiveMaxHealthDamage),
        "Max Stamina Damage: "+istr(s.explosiveStaminaDamage),
        "Max Hard Damage: "+fmtNum(s.explosiveHardDamage),
        "Wind Up Time: "+fmtSec(s.windUpTime),
        "Reload Time: "+fmtSec(s.reloadTime),
        "Equip Time: "+fmtSec(s.equipTime),
        "Air Burst Distance: "+fmtM(s.explosiveAirBurst)},SEP)+"\n";
}
static std::string burstFmt(const std::string& k,const Stats& s){
    int bs=s.burstShots; std::string xs=" x "+std::to_string(bs);
    return pfx(k)+join({
        "Time to Kill: "+fmtSec(s.burstTtk),
        "Shots to Kill: "+istr(s.stk),
        "Health Damage: "+istr(s.healthDamage)+xs,
        "Stamina Damage: "+istr(s.staminaDamage)+xs,
        "Hard Damage: "+fmtNum(s.hardDamage)+xs,
        "Effective Range: "+fmtM(s.rampDistanceInM),
        "Burst Interval: "+fmtSec(s.burstInterval),
        "Reload Time: "+fmtSec(s.reloadTime),
        "Equip Time: "+fmtSec(s.equipTime)},SEP)+"\n";
}
static std::string grenadeFmt(const std::string& k,const Stats& s){
    return pfx(k)+join({
        "Max Health Damage: "+istr(s.explosiveMaxHealthDamage),
        "Max Stamina Damage: "+istr(s.explosiveStaminaDamage),
        "Max Hard Damage: "+fmtNum(s.explosiveHardDamage),
        "Explosion Radius: "+fmtCm(s.explosiveFRadius),
        "Max Damage Radius: "+fmtCm(s.explosiveFGroundZeroRadius),
        "Fuse Delay: "+fmtSec(s.fuseDelay),
        "Speed: "+fmtMps(s.firingSpeed/100.0)},SEP)+"\n";
}

// ── Colorized ───────────────────────────────────────────────────────────────
static std::string applyLabel(const std::string& txt,Scheme sc,const RGB& si,const RGB& gs,const RGB& ge){
    if(sc==Scheme::WRITCH)    return gradientText(txt,WRITCH_START(),WRITCH_END());
    if(sc==Scheme::SPELLBOUND)return gradientText(txt,SPELL_START(),SPELL_END());
    if(sc==Scheme::GRADIENT)  return gradientText(txt,gs,ge);
    if(sc==Scheme::TRIPLE)    return tripleGradientText(txt,gs,si,ge);
    if(sc==Scheme::SINGLE)    return solidText(txt,si);
    return txt;
}

static std::string colorized(const std::string& k,const Stats& s,Scheme sc,RGB si,RGB gs,RGB ge){
    auto L=[&](const std::string& t){return applyLabel(t,sc,si,gs,ge);};
    std::vector<std::string> parts;
    if(s.isRl){
        parts={L("Time To Kill:")+" "+fmtSec(s.explosiveTtk),L("Shots to Kill:")+" "+istr(s.explosiveStk),
               L("Max Health Damage:")+" "+istr(s.explosiveMaxHealthDamage),L("Max Stamina Damage:")+" "+istr(s.explosiveStaminaDamage),
               L("Max Hard Damage:")+" "+fmtNum(s.explosiveHardDamage),L("Wind Up Time:")+" "+fmtSec(s.windUpTime),
               L("Reload Time:")+" "+fmtSec(s.reloadTime),L("Equip Time:")+" "+fmtSec(s.equipTime),
               L("Air Burst Distance:")+" "+fmtM(s.explosiveAirBurst)};
    } else if(s.isGlLtl){
        parts={L("Time to Stun:")+" "+fmtSec(s.explosiveTts),L("Shots to Stun:")+" "+istr(s.explosiveSts),
               L("Max Health Damage:")+" "+istr(s.explosiveMaxHealthDamage),L("Max Stamina Damage:")+" "+istr(s.explosiveStaminaDamage),
               L("Max Hard Damage:")+" "+fmtNum(s.explosiveHardDamage),L("Wind Up Time:")+" "+fmtSec(s.windUpTime),
               L("Fire Interval:")+" "+fmtSec(s.fireInterval),L("Reload Time:")+" "+fmtSec(s.reloadTime),L("Equip Time:")+" "+fmtSec(s.equipTime)};
    } else if(s.isGl){
        parts={L("Time To Kill:")+" "+fmtSec(s.explosiveTtk),L("Shots to Kill:")+" "+istr(s.explosiveStk),
               L("Max Health Damage:")+" "+istr(s.explosiveMaxHealthDamage),L("Max Stamina Damage:")+" "+istr(s.explosiveStaminaDamage),
               L("Max Hard Damage:")+" "+fmtNum(s.explosiveHardDamage),L("Wind Up Time:")+" "+fmtSec(s.windUpTime),
               L("Fire Interval:")+" "+fmtSec(s.fireInterval),L("Reload Time:")+" "+fmtSec(s.reloadTime),L("Equip Time:")+" "+fmtSec(s.equipTime)};
    } else if(s.isThrownGrenade){
        parts={L("Max Health Damage:")+" "+istr(s.explosiveMaxHealthDamage),L("Max Stamina Damage:")+" "+istr(s.explosiveStaminaDamage),
               L("Max Hard Damage:")+" "+fmtNum(s.explosiveHardDamage),L("Explosion Radius:")+" "+fmtCm(s.explosiveFRadius),
               L("Max Damage Radius:")+" "+fmtCm(s.explosiveFGroundZeroRadius),L("Fuse Delay:")+" "+fmtSec(s.fuseDelay),
               L("Speed:")+" "+fmtMps(s.firingSpeed/100.0)};
    } else if(s.isLtlAmmoWeapon){
        parts={L("Time to Stun:")+" "+fmtSec(s.tts),L("Shots to Stun:")+" "+istr(s.sts),
               L("Health Damage:")+" "+istr(s.healthDamage),L("Stamina Damage:")+" "+istr(s.staminaDamage),
               L("Hard Damage:")+" "+fmtNum(s.hardDamage),L("Effective Range:")+" "+fmtM(s.ltlRange),
               L("Fire Interval:")+" "+fmtSec(s.fireInterval),L("Reload Time:")+" "+fmtSec(s.reloadTime),L("Equip Time:")+" "+fmtSec(s.equipTime)};
    } else if(s.burstShots>1){
        std::string xs=" x "+std::to_string(s.burstShots);
        parts={L("Time to Kill:")+" "+fmtSec(s.burstTtk),L("Shots to Kill:")+" "+istr(s.stk),
               L("Health Damage:")+" "+istr(s.healthDamage)+xs,L("Stamina Damage:")+" "+istr(s.staminaDamage)+xs,
               L("Hard Damage:")+" "+fmtNum(s.hardDamage)+xs,L("Effective Range:")+" "+fmtM(s.rampDistanceInM),
               L("Burst Interval:")+" "+fmtSec(s.burstInterval),L("Reload Time:")+" "+fmtSec(s.reloadTime),L("Equip Time:")+" "+fmtSec(s.equipTime)};
    } else {
        int p=std::max(1,s.nPellets); std::string xs=p==1?"":" x "+std::to_string(p);
        parts={L("Time to Kill:")+" "+fmtSec(s.ttk),L("Shots to Kill:")+" "+istr(s.stk),
               L("Health Damage:")+" "+istr(s.healthDamage)+xs,L("Stamina Damage:")+" "+istr(s.staminaDamage)+xs,
               L("Hard Damage:")+" "+fmtNum(s.hardDamage)+xs,L("Effective Range:")+" "+fmtM(s.rampDistanceInM),
               L("Fire Interval:")+" "+fmtSec(s.fireInterval),L("Reload Time:")+" "+fmtSec(s.reloadTime),L("Equip Time:")+" "+fmtSec(s.equipTime)};
    }
    return "WeaponItemTypes_"+k+"_Description="+join(parts,SEP)+"\n";
}

std::string formatFromInfra(const std::string& k,const Stats& s,Scheme sc,RGB si,RGB gs,RGB ge){
    if(sc==Scheme::CLEAR){
        if(s.isRl)            return rocketFmt(k,s);
        if(s.isGlLtl)         return glLtlFmt(k,s);
        if(s.isGl)            return glFmt(k,s);
        if(s.isThrownGrenade) return grenadeFmt(k,s);
        if(s.isLtlAmmoWeapon) return ltlFmt(k,s);
        if(s.burstShots>1)    return burstFmt(k,s);
        if(s.nPellets>1)      return shotgunFmt(k,s);
        return defaultFmt(k,s);
    }
    return colorized(k,s,sc,si,gs,ge);
}
} // namespace apb
