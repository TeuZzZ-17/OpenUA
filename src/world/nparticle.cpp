#include "nparticle.h"
#include "../system/inivals.h"
#include "../skeleton.h"
#include "../ade.h"
#include "../particle.h"
#include "../base.h"
#include "../utils.h"
#include "spin.h"

#include <cmath>

namespace World
{

ParticleSystem::ParticleSystem()
{
}

static float ParticleSystem_MaxScaleAxis(const vec3d &scale)
{
    float maxScale = scale.x;

    if ( scale.y > maxScale )
        maxScale = scale.y;
    if ( scale.z > maxScale )
        maxScale = scale.z;

    return maxScale > 0.0f ? maxScale : 1.0f;
}

void ParticleSystem::AddParticle(NC_STACK_particle *base, const vec3d& pos, const vec3d& vec, int32_t age, const GFX::TGLColor &tint, const vec3d &scale, const vec3d &spin, float lifetimeScale)
{
    if ( !std::isfinite(lifetimeScale) || lifetimeScale <= 0.0f )
        lifetimeScale = 1.0f;

    if (!_disableAdd && (int32_t)_particles.size() < System::IniConf::ParticlesLimit.Get<int32_t>())
        _particles.emplace_back( base, pos, vec, age, tint, scale, spin, lifetimeScale );
}

void ParticleSystem::UpdateRender(area_arg_65 *rndrParams, int32_t delta)
{
    //Prevent spawn new particles from particle render
    _disableAdd = true;

    float fsec = (float)delta * 0.001;

    for(auto it = _particles.begin(); it != _particles.end();)
    {
        Frak &f = *it;
        if (!f.pParticleGen)
        {
            it = _particles.erase(it);
            continue;
        }

        f.Age += delta;
        float scaledLifeTime = (float)f.pParticleGen->_lifeTime * f.LifetimeScale;

        if ((float)f.Age >= scaledLifeTime)
        {
            it = _particles.erase(it);
            continue;
        }

        int32_t visualAge = (int32_t)((float)f.Age / f.LifetimeScale);

        // The acceleration and noise terms were historically applied once per
        // rendered frame, tuned for the vanilla ~60fps frame of ~17 clock units
        // (1024 units/s). Scale them by the actual frame delta so particles keep
        // the same trajectories at any frame rate: at 60fps frameScale ~= 1.0
        // (vanilla behavior), at 240fps each of the 4x more frequent steps
        // contributes a quarter.
        float frameScale = (float)delta * (60.0f / 1024.0f);

        f.Vec += (f.pParticleGen->_accelStart + f.pParticleGen->_accelDelta * visualAge) * frameScale;
        f.Pos += f.Vec * fsec + NC_STACK_particle::RandVec() * (f.pParticleGen->_noisePower * frameScale);
        float baseScale = f.pParticleGen->_scaleStart + f.pParticleGen->_scaleDelta * visualAge;
        vec3d scl(baseScale * f.Scale.x, baseScale * f.Scale.y, baseScale * f.Scale.z);

        Render(&f, scl, rndrParams, visualAge);

        it++;
    }

    _disableAdd = false;
}


void ParticleSystem::Render(Frak *p, const vec3d &scale, area_arg_65 *rndrParams, int32_t visualAge)
{
    TF::TForm3D *view = rndrParams->ViewTForm;

    // Transformed position, but without of projection
    vec3d pos = view->CalcSclRot.Transform(p->Pos - view->CalcPos);

    if (pos.z < GFX::Engine.GetProjectionNear() || pos.z > GFX::Engine.GetProjectionFar())
        return;

    // Calculate projection position and W parameter for NDC
    const mat4x4f &Proj = GFX::Engine.GetProjectionMatrix();

    vec3d projPos = Proj.Transform(pos);
    float w = Proj.CalcW(pos);

    /* Radius of 2.0 side square = sqrt(1.0^2 + 1.0^2) = 1.414
     * And applied W
     */
    float maxScale = ParticleSystem_MaxScaleAxis(scale);
    float radius = maxScale * 1.42 / w;

    vec2f scrPos(projPos.x / w, projPos.y / w);

    // If transformed point length more than
    if ( scrPos.length() > 1.42 + radius )
        return;

    if ( p->pParticleGen->_lifePerAde )
    {
        size_t id = visualAge / p->pParticleGen->_lifePerAde;

        if (id == p->pParticleGen->_meshCache.size()) // fix little overlap
            id = p->pParticleGen->_meshCache.size() - 1;

        if ( id < p->pParticleGen->_meshCache.size() )
        {
            GFX::TMesh &mesh = p->pParticleGen->_meshCache.at(id);

            GFX::TRenderNode& rend = GFX::Engine.AllocRenderNode();
            rend = GFX::TRenderNode( GFX::TRenderNode::TYPE_PARTICLE );

            rend.Distance = pos.length();
            rend.Flags = mesh.Mat.Flags | rndrParams->flags;
            rend.Color = mesh.Mat.Color;
            rend.ColorMul = p->Tint;

            if ((mesh.Mat.Flags & GFX::RFLAGS_DYNAMIC_TEXTURE) && mesh.Mat.TexSource)
            {
                mesh.Mat.TexSource->SetTime(rndrParams->timeStamp, rndrParams->frameTime);
                uint32_t frameid = mesh.Mat.TexSource->GetCurrentFrameID();

                if (frameid < mesh.CoordsCache.size())
                {
                    rend.Tex = mesh.CoordsCache.at(frameid).Tex;
                    rend.coordsID = frameid;
                }
            }
            else
                rend.Tex = mesh.Mat.Tex;

            rend.Mesh = &mesh;

            mat4x4 particleForm(Spin::BuildMatrix(p->Spin, visualAge) * mat3x3::Scale(scale));
            particleForm.m03 = pos.x;
            particleForm.m13 = pos.y;
            particleForm.m23 = pos.z;
            rend.TForm = particleForm;
            rend.TimeStamp = rndrParams->timeStamp;
            rend.FrameTime = rndrParams->frameTime;
            rend.FogStart = rndrParams->fadeStart;
            rend.FogLength = rndrParams->fadeLength;
            rend.ParticleSize = maxScale;

            GFX::GFXEngine::Instance.QueueRenderMesh(&rend);
        }
    }
}

void ParticleSystem::Clear()
{
    _particles.clear();
}

}
