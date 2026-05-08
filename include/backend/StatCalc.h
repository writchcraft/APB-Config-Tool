#pragma once
#include <string>
#include <vector>
#include <limits>
#include <cmath>

namespace apb {

constexpr double HEALTH_POOL  = 1000.0;
constexpr double STAMINA_POOL = 1000.0;
constexpr double INF_VAL      = std::numeric_limits<double>::infinity();

struct Stats {
    double ttk=0; int stk=0;
    double healthDamage=0, staminaDamage=0, hardDamage=0;
    double rampDistanceInM=0, fireInterval=0, reloadTime=0, equipTime=0;
    int    nPellets=1, nMagazineCapacity=0;
    int    burstShots=1; double burstInterval=0, burstTtk=0;
    int    sts=0; double tts=0, ltlRange=0;
    double explosiveTtk=0, explosiveTts=0;
    int    explosiveStk=0, explosiveSts=0;
    double explosiveMaxHealthDamage=0, explosiveStaminaDamage=0, explosiveHardDamage=0;
    double explosiveFRadius=0, explosiveFGroundZeroRadius=0;
    double armingTimer=0, fuseDelay=0, firingSpeed=0, windUpTime=0, explosiveAirBurst=0;
    bool isThrownGrenade=false, isGl=false, isGlLtl=false, isRl=false, isLtlAmmoWeapon=false;
};

struct StatsResult { std::string infra; Stats stats; bool ok=false; };

struct ModEffect {
    int    eEffectType       = 0;
    double fAddToResult      = 0.0;
    double fEffectMultiplier = 1.0;
};

int    calculateStk(double dmgPerShot, double pool = HEALTH_POOL);
double calculateTtk(double dmgPerShot, double fireInterval, double pool = HEALTH_POOL);
double calculateTtkWithWindup(const Stats& s, double pool = HEALTH_POOL);
int    calculateSts(double stamPerShot, double pool = STAMINA_POOL);
double calculateTtkLtlWithStats(const Stats& s, double pool = STAMINA_POOL);
double burstCalculateTtk(const Stats& s);
int    explosiveCalculateStk(double maxHealthDamage, double pool = HEALTH_POOL);
int    explosiveCalculateSts(double expStaminaDamage, double pool = STAMINA_POOL);
double explosiveCalculateTtkRust(const Stats& s, bool isLtl);
double calculateAirBurstRustParity(double firingSpeedCmps, double fuseDelayS);
void   applyMods(Stats& s, const std::vector<ModEffect>& mods, double& hm, double& hmExpl);

StatsResult statsFromApbdb(const std::string& extracted, int timeoutSec = 15);

} // namespace apb
