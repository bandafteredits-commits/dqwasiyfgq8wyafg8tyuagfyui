#include "../cpbob_core.h"
#include <cstdio>
using namespace cpbob;
int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); ++fails; } }while(0)
static float peak(float fps, const Settings& s, bool* monoOk){
    BobAnim a; a.trigger(); float dt=1.0f/fps, lo=0, prev=0; bool down=true; *monoOk=true;
    for(int i=0;i<(int)(fps*0.6f);i++){
        float o=a.step(s,dt); if(o<lo)lo=o;
        if(down && o>prev+1e-6f) down=false;            // after the lowest point it may only rise
        if(!down && o<prev-1e-6f) *monoOk=false;
        prev=o;
    }
    CHECK(!a.active()); return lo;
}
int main(){
    // crit rule
    PlayerState p; CHECK(!isCrit(p));                     // standing
    p.onGround=false; p.fallDistance=0.4f; CHECK(isCrit(p));
    PlayerState w=p; w.inLiquid=true; CHECK(!isCrit(w));
    PlayerState l=p; l.onLadder=true; CHECK(!isCrit(l));
    PlayerState r=p; r.riding=true;  CHECK(!isCrit(r));
    PlayerState rising=p; rising.fallDistance=0; CHECK(!isCrit(rising)); // still going up
    // animation
    Settings s; s.enabled=true;
    BobAnim a; CHECK(a.step(s,0.016f)==0.0f);             // no trigger -> no offset
    bool mono; float pk=peak(60,s,&mono);
    CHECK(pk<=-s.depth*0.97f && pk>=-s.depth*1.001f); CHECK(mono);
    float pk250=peak(250,s,&mono), pk30=peak(30,s,&mono);
    CHECK(std::fabs(pk250-pk)<s.depth*0.05f); CHECK(std::fabs(pk30-pk)<s.depth*0.25f); // frame-rate independent
    BobAnim b; b.trigger(); b.step(s,0.1f); float mid=b.step(s,0.0f); CHECK(mid<0);
    b.trigger(); CHECK(b.step(s,0.0f)==0.0f);             // re-trigger restarts from the top
    Settings off=s; off.enabled=false; BobAnim c; c.trigger(); CHECK(c.step(off,0.05f)==0.0f && !c.active());
    // settings file parsing
    bool on=false; unsigned char okOn[8]={0x43,0x42,0x4F,0x42,1,0,0,0}, okOff[8]={0x43,0x42,0x4F,0x42,0,0,0,0};
    unsigned char bad[8]={0x43,0x43,0x55,0x4C,1,0,0,0};   // the culling file's magic must NOT turn bob on
    CHECK(parseCfg(okOn,8,&on) && on); CHECK(parseCfg(okOff,8,&on) && !on);
    on=true; CHECK(!parseCfg(bad,8,&on)); CHECK(!parseCfg(okOn,7,&on)); CHECK(!parseCfg(nullptr,8,&on));
    printf("peak@60=%.4f peak@250=%.4f peak@30=%.4f\n",pk,pk250,pk30);
    printf(fails?"FAILED\n":"ALL PASS\n"); return fails;
}
