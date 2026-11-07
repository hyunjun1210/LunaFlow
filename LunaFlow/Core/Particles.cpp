#include "Core/Particles.h"
#include "Core/EditorCommands.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <stdexcept>

namespace LunaFlow {
ParticleSettings MakeParticleSettings(ParticlePreset preset) {
    ParticleSettings p;
    switch(preset){
    case ParticlePreset::Fire:p.rate=95;p.speed=1.6f;p.startSize=.19f;p.endSize=.025f;p.angle=15;break;
    case ParticlePreset::Smoke:p.rate=28;p.lifetime=2.4f;p.speed=.5f;p.startSize=.16f;p.endSize=.65f;p.radius=.2f;p.additive=false;p.endColor={.22f,.25f,.3f,0};break;
    case ParticlePreset::Magic:p.shape=ParticleShape::Sphere;p.speed=.7f;p.rate=65;p.radius=.35f;p.startSize=.09f;p.endSize=.01f;p.endColor={.16f,.65f,1,0};break;
    case ParticlePreset::Snow:p.shape=ParticleShape::Box;p.box={3,.1f,3};p.speed=0;p.velocity={0,-.6f,0};p.rate=45;p.lifetime=4;p.startSize=.045f;p.endSize=.03f;p.additive=false;p.endColor={.75f,.9f,1,0};break;
    case ParticlePreset::Rain:p.shape=ParticleShape::Box;p.box={3,.1f,3};p.speed=0;p.velocity={0,-4,0};p.rate=180;p.lifetime=1.3f;p.startSize=.025f;p.endSize=.015f;p.additive=false;p.endColor={.35f,.6f,1,0};break;
    case ParticlePreset::Explosion:p.shape=ParticleShape::Sphere;p.radius=.06f;p.speed=3;p.gravity=1;p.rate=0;p.burst=180;p.loop=false;p.lifetime=1.2f;p.startSize=.1f;p.endSize=.01f;break;
    default:break;
    }return p;
}
EffectData MakeParticleEffect(ParticlePreset preset,uint32_t bone,const std::string& clipId,double duration) {
    EffectData e;e.id=NewClipId();e.type=EffectType::Particles;e.bone=bone;e.clipId=clipId;
    e.end=std::max(duration,.1);e.size=1;e.particles=MakeParticleSettings(preset);
    e.particles.duration=static_cast<float>(std::min(2.0,e.end));
    switch(preset){
    case ParticlePreset::Fire:e.name="Fire";e.color={1,.56f,.08f,1};break;
    case ParticlePreset::Smoke:e.name="Smoke";e.color={.55f,.58f,.62f,.42f};break;
    case ParticlePreset::Magic:e.name="Magic";e.color={.7f,.3f,1,1};break;
    case ParticlePreset::Snow:e.name="Snow";e.color={.95f,.98f,1,.9f};e.local.translation.y=2;break;
    case ParticlePreset::Rain:e.name="Rain";e.color={.5f,.7f,1,.65f};e.local.translation.y=3;break;
    case ParticlePreset::Explosion:e.name="Explosion";e.color={1,.8f,.15f,1};break;
    default:e.name="Particle System";e.color={1,.65f,.2f,1};break;
    }return e;
}
void ValidateParticles(const ParticleSettings& p) {
    auto range=[](float v,float lo,float hi){if(!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error("Particle value is outside the supported range.");};
    if(static_cast<int>(p.shape)<0||static_cast<int>(p.shape)>3)throw std::runtime_error("Unknown particle shape.");
    range(p.duration,.05f,120);range(p.rate,0,2000);range(p.lifetime,.05f,20);range(p.lifetimeRandomness,0,.95f);
    range(p.speed,0,50);range(p.speedRandomness,0,1);range(p.gravity,-5,5);range(p.radius,0,20);range(p.angle,0,90);
    range(p.startSize,.001f,10);range(p.endSize,0,10);
    for(float v:{p.box.x,p.box.y,p.box.z})range(v,0,40);
    for(float v:{p.velocity.x,p.velocity.y,p.velocity.z})range(v,-50,50);
    for(float v:{p.endColor.x,p.endColor.y,p.endColor.z,p.endColor.w})range(v,0,1);
    if(p.maxParticles<1||p.maxParticles>4096||p.burst<0||p.burst>4096||p.seed==0)throw std::runtime_error("Invalid particle count or random seed.");
}
Json EncodeParticles(const ParticleSettings& p) {
    ValidateParticles(p);
    return {{"shape",static_cast<int>(p.shape)},{"duration",p.duration},{"rate",p.rate},{"lifetime",p.lifetime},{"lifetimeRandomness",p.lifetimeRandomness},
        {"speed",p.speed},{"speedRandomness",p.speedRandomness},{"gravity",p.gravity},{"radius",p.radius},{"angle",p.angle},
        {"startSize",p.startSize},{"endSize",p.endSize},{"box",{p.box.x,p.box.y,p.box.z}},{"velocity",{p.velocity.x,p.velocity.y,p.velocity.z}},
        {"endColor",{p.endColor.x,p.endColor.y,p.endColor.z,p.endColor.w}},{"seed",p.seed},{"maxParticles",p.maxParticles},{"burst",p.burst},
        {"loop",p.loop},{"worldSpace",p.worldSpace},{"additive",p.additive}};
}
ParticleSettings DecodeParticles(const Json& j) {
    ParticleSettings p;p.shape=static_cast<ParticleShape>(j.at("shape").get<int>());
    p.duration=j.at("duration");p.rate=j.at("rate");p.lifetime=j.at("lifetime");p.lifetimeRandomness=j.at("lifetimeRandomness");
    p.speed=j.at("speed");p.speedRandomness=j.at("speedRandomness");p.gravity=j.at("gravity");p.radius=j.at("radius");p.angle=j.at("angle");
    p.startSize=j.at("startSize");p.endSize=j.at("endSize");
    const auto box=j.at("box").get<std::array<float,3>>(),velocity=j.at("velocity").get<std::array<float,3>>();
    p.box={box[0],box[1],box[2]};p.velocity={velocity[0],velocity[1],velocity[2]};
    const auto color=j.at("endColor").get<std::array<float,4>>();p.endColor={color[0],color[1],color[2],color[3]};
    p.seed=j.at("seed").get<uint32_t>();p.maxParticles=j.at("maxParticles");p.burst=j.at("burst");
    p.loop=j.at("loop");p.worldSpace=j.at("worldSpace");p.additive=j.at("additive");ValidateParticles(p);return p;
}
static uint32_t Hash(uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
std::vector<ParticleSample> SampleParticles(const EffectData& e,double time) {
    std::vector<ParticleSample> result;
    if(!e.enabled||e.type!=EffectType::Particles||!std::isfinite(time)||time<e.start||time>e.end)return result;
    const auto& p=e.particles;ValidateParticles(p);
    const double t=time-e.start,oldest=std::max(0.0,t-p.lifetime);
    // At most 401 cycles (20-second maximum lifetime / .05-second minimum duration).
    const auto first=p.loop?static_cast<uint64_t>(oldest/p.duration):0;
    const auto last=p.loop?static_cast<uint64_t>(t/p.duration):0;
    result.reserve(static_cast<size_t>(p.maxParticles));
    auto emit=[&](uint64_t cycle,uint32_t index,double birth){
        const double age=t-birth;if(age<0||age>=p.lifetime||result.size()>=static_cast<size_t>(p.maxParticles))return;
        uint32_t random=Hash(p.seed^Hash(static_cast<uint32_t>(cycle))^Hash(index+17));
        auto rnd=[&](){random=Hash(random+0x9e3779b9u);return (random>>8)*(1.0f/16777216.0f);};
        const float life=p.lifetime*(1-p.lifetimeRandomness*rnd());if(age>=life)return;
        const float z=2*rnd()-1,theta=rnd()*XM_2PI,ring=std::sqrt(std::max(0.0f,1-z*z));
        XMFLOAT3 direction{ring*std::cos(theta),z,ring*std::sin(theta)},position{};
        if(p.shape==ParticleShape::Sphere){const float r=p.radius*std::cbrt(rnd());position={direction.x*r,direction.y*r,direction.z*r};}
        else if(p.shape==ParticleShape::Box){position={(rnd()-.5f)*p.box.x,(rnd()-.5f)*p.box.y,(rnd()-.5f)*p.box.z};direction={0,1,0};}
        else {
            const float r=p.radius*(p.shape==ParticleShape::Circle?1.0f:std::sqrt(rnd()));position={std::cos(theta)*r,0,std::sin(theta)*r};
            if(p.shape==ParticleShape::Circle)direction={std::cos(theta),0,std::sin(theta)};
            else {const float cosine=1-rnd()*(1-std::cos(XMConvertToRadians(p.angle))),sine=std::sqrt(std::max(0.0f,1-cosine*cosine));direction={std::cos(theta)*sine,cosine,std::sin(theta)*sine};}
        }
        const float speed=p.speed*(1-p.speedRandomness*rnd()),a=static_cast<float>(age),u=a/life;
        position.x+=(direction.x*speed+p.velocity.x)*a;
        position.y+=(direction.y*speed+p.velocity.y)*a-4.905f*p.gravity*a*a;
        position.z+=(direction.z*speed+p.velocity.z)*a;
        auto lerp=[&](float x,float y){return x+(y-x)*u;};
        result.push_back({position,{lerp(e.color.x,p.endColor.x)*e.intensity,lerp(e.color.y,p.endColor.y)*e.intensity,lerp(e.color.z,p.endColor.z)*e.intensity,lerp(e.color.w,p.endColor.w)},lerp(p.startSize,p.endSize)*e.size,birth});
    };
    // Newest births win the deterministic preview budget; no unbounded history is walked.
    for(uint64_t cycle=last;;--cycle){
        const double base=cycle*p.duration;
        if(p.rate>0){
            const auto begin=static_cast<uint32_t>(std::max(1.0,std::floor(std::max(0.0,oldest-base)*p.rate)));
            const auto end=static_cast<uint32_t>(std::floor(std::min(t-base,static_cast<double>(p.duration))*p.rate+1e-6));
            for(uint32_t n=end;n>=begin&&n>0&&result.size()<static_cast<size_t>(p.maxParticles);--n){
                const double birth=base+n/static_cast<double>(p.rate);if(birth<base+p.duration)emit(cycle,n,birth);
            }
        }
        if(base>=oldest)for(int n=0;n<p.burst&&result.size()<static_cast<size_t>(p.maxParticles);++n)emit(cycle,0x80000000u+static_cast<uint32_t>(n),base);
        if(cycle==first||cycle==0||result.size()>=static_cast<size_t>(p.maxParticles))break;
    }return result;
}
EffectData GenerateParticleEffect(const std::string& prompt,uint32_t bone,const std::string& clipId,double duration) {
    if(prompt.empty()||prompt.size()>4096||prompt.find_first_not_of(" \t\r\n")==std::string::npos)throw std::runtime_error("Enter a particle description.");
    std::string text=prompt;for(auto& c:text)if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto has=[&](std::initializer_list<const char*> words){for(auto word:words)if(text.find(word)!=std::string::npos)return true;return false;};
    ParticlePreset preset;
    if(has({"폭발","불꽃놀이","explosion","explode","burst","firework"}))preset=ParticlePreset::Explosion;
    else if(has({"연기","smoke","안개","fog"}))preset=ParticlePreset::Smoke;
    else if(has({"눈송이","눈이","눈 내","snow"}))preset=ParticlePreset::Snow;
    else if(has({"빗방울","비가","비 내","rain"}))preset=ParticlePreset::Rain;
    else if(has({"불꽃","불길","화염","fire","flame","spark","스파크"}))preset=ParticlePreset::Fire;
    else if(has({"마법","반짝","오라","magic","aura","glitter","에너지","energy"}))preset=ParticlePreset::Magic;
    else if(has({"파티클","입자","particle"}))preset=ParticlePreset::Custom;
    else throw std::runtime_error("Use fire, smoke, magic, snow, rain, explosion or particle (불꽃, 연기, 마법, 눈송이, 빗방울, 폭발, 파티클).");
    auto e=MakeParticleEffect(preset,bone,clipId,duration);e.sourcePrompt=prompt;auto& p=e.particles;
    const std::pair<std::vector<const char*>,XMFLOAT4> colors[]={
        {{"빨간","빨강","red"},{1,.08f,.04f,1}},{{"파란","파랑","푸른","blue"},{.12f,.45f,1,1}},
        {{"보라","purple","violet"},{.65f,.2f,1,1}},{{"초록","녹색","green"},{.15f,1,.3f,1}},
        {{"노란","노랑","yellow"},{1,.85f,.1f,1}},{{"하얀","흰색","white"},{1,1,1,1}},{{"분홍","pink"},{1,.3f,.65f,1}}};
    for(const auto& color:colors)if(std::any_of(color.first.begin(),color.first.end(),[&](const char* word){return text.find(word)!=std::string::npos;})){
        e.color=color.second;p.endColor={e.color.x,e.color.y,e.color.z,0};break;
    }
    if(has({"천천히","느리게","slow"})){p.speed*=.4f;p.velocity.x*=.4f;p.velocity.y*=.4f;p.velocity.z*=.4f;}
    if(has({"빠르게","빠른","fast"})){p.speed*=2;p.velocity.x*=2;p.velocity.y*=2;p.velocity.z*=2;}
    if(has({"풍성","많이","dense"}))p.rate*=2;
    if(has({"구형","구 모양","sphere"}))p.shape=ParticleShape::Sphere;
    if(has({"원뿔","cone"}))p.shape=ParticleShape::Cone;
    if(has({"상자","box"}))p.shape=ParticleShape::Box;
    if(has({"원형","고리","circle","ring"}))p.shape=ParticleShape::Circle;
    if(has({"한 번","한번","once","폭발","explosion"}))p.loop=false;
    if(has({"반복","loop"}))p.loop=true;
    if(has({"월드","world"}))p.worldSpace=true;
    auto number=[&](const std::string& expression,float& value){std::smatch match;if(std::regex_search(text,match,std::regex(expression)))value=std::stof(match[1].str());};
    const std::string num="(-?[0-9]+(?:\\.[0-9]+)?)";
    number("(?:초당\\s*|rate\\s*[:=]?\\s*)"+num,p.rate);
    number("(?:수명\\s*|lifetime\\s*[:=]?\\s*)"+num,p.lifetime);
    number("(?:속도\\s*|speed\\s*[:=]?\\s*)"+num,p.speed);
    number("(?:크기\\s*|size\\s*[:=]?\\s*)"+num,p.startSize);
    number("(?:반경\\s*|radius\\s*[:=]?\\s*)"+num,p.radius);
    number("(?:퍼짐\\s*|angle\\s*[:=]?\\s*)"+num,p.angle);
    number("(?:방출\\s*|duration\\s*[:=]?\\s*)"+num,p.duration);
    std::smatch length;if(std::regex_search(text,length,std::regex(num+"\\s*(?:초 동안|초간|seconds?|secs?)")))e.end=std::min(duration,static_cast<double>(std::stof(length[1].str())));
    float burst=static_cast<float>(p.burst);number("(?:한번에\\s*|한 번에\\s*|burst\\s*[:=]?\\s*)"+num,burst);
    if(burst<0||burst>4096)throw std::runtime_error("Burst must be between 0 and 4096 particles.");p.burst=static_cast<int>(burst);
    if(e.end<=0)throw std::runtime_error("Effect length must be positive.");
    ValidateParticles(p);return e;
}
}
