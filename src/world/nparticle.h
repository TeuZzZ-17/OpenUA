#ifndef WORLD_NPARTICLE_H_INCLUDED
#define WORLD_NPARTICLE_H_INCLUDED

#include <deque>
#include "../system/tform.h"
#include "../system/gfx.h"
#include "../vectors.h"

class NC_STACK_particle;
class NC_STACK_skeleton;
namespace UAskeleton { struct Data; };
struct area_arg_65;

namespace World
{

class ParticleSystem
{
public:

protected:
    struct Frak
    {
        NC_STACK_particle *pParticleGen = NULL;

        vec3d Pos;
        vec3d Vec;

        int32_t Age = 0;
        GFX::TGLColor Tint = GFX::TGLColor(1.0, 1.0, 1.0, 1.0);
        vec3d Scale = vec3d(1.0, 1.0, 1.0);
        vec3d Spin = vec3d(0.0, 0.0, 0.0);

        Frak(NC_STACK_particle *base, const vec3d& pos, const vec3d& vec, int32_t age = 0, const GFX::TGLColor &tint = GFX::TGLColor(1.0, 1.0, 1.0, 1.0), const vec3d &scale = vec3d(1.0, 1.0, 1.0), const vec3d &spin = vec3d(0.0, 0.0, 0.0))
        : pParticleGen(base), Pos(pos), Vec(vec), Age(age), Tint(tint),
          Scale(scale.x > 0.0 ? scale.x : 1.0,
                scale.y > 0.0 ? scale.y : 1.0,
                scale.z > 0.0 ? scale.z : 1.0),
          Spin(spin)
        {};
    };

public:
    ParticleSystem();

    void AddParticle(NC_STACK_particle *base, const vec3d& pos, const vec3d& vec, int32_t age = 0, const GFX::TGLColor &tint = GFX::TGLColor(1.0, 1.0, 1.0, 1.0), const vec3d &scale = vec3d(1.0, 1.0, 1.0), const vec3d &spin = vec3d(0.0, 0.0, 0.0));

    void UpdateRender(area_arg_65 *rndrParams, int32_t delta);

    void Clear();

protected:
    void Render(Frak *p, const vec3d &scale, area_arg_65 *rndrParams);


protected:
    std::deque<Frak> _particles;

    bool _disableAdd = false;
};


}

#endif
