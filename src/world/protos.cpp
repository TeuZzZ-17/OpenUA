#include "protos.h"
#include "../env.h"
#include "../utils.h"
#include "../log.h"
#include "../wav.h"
#include "../ypabact.h"

namespace World
{
static int SampleFrameSize(ALenum format)
{
    switch (format)
    {
        case AL_FORMAT_MONO8:
            return 1;

        case AL_FORMAT_STEREO8:
        case AL_FORMAT_MONO16:
            return 2;

        case AL_FORMAT_STEREO16:
            return 4;

        default:
            return 1;
    }
}

uint8_t DestFX::ParseTypeName(const std::string &in)
{
    if ( !StriCmp(in, "death") )
        return FX_DEATH;
    
    if ( !StriCmp(in, "megadeth") )
        return FX_MEGADETH;
    
    if ( !StriCmp(in, "create") )
        return FX_CREATE;
    
    if ( !StriCmp(in, "beam") )
        return FX_BEAM;
    
    return FX_NONE;
}


void TVhclSound::LoadSamples()
{
    bool hasLoadedVariant = false;
    for (const TSndSample &sample : MainSampleVariants)
    {
        if (sample.Sample)
        {
            hasLoadedVariant = true;
            break;
        }
    }

    if ( !MainSample.Sample && !hasLoadedVariant && (ExtSamples.empty() || !ExtSamples.at(0).Sample) )
    {
        std::string oldRsrc = Common::Env.SetPrefix("rsrc", "data:");

        if ( !extS.empty() )
        {
            for (size_t i = 0; i < extS.size(); i++)
            {
                TSampleParams &pprm = extS.at(i);
                
                ExtSamples.at(i).Sample = Nucleus::CInit<NC_STACK_wav>( {{NC_STACK_rsrc::RSRC_ATT_NAME, ExtSamples.at(i).Name}} );

                if ( ExtSamples.at(i).Sample )
                {
                    TSampleData *sample = ExtSamples.at(i).Sample->GetSampleData();
                    int frameSize = SampleFrameSize(sample->Format);

                    pprm.Sample = sample;
                    pprm.rlOffset = (sample->SampleRate * pprm.Offset / 11000) * frameSize;
                    pprm.rlSmplCnt = (sample->SampleRate * pprm.SampleCnt / 11000) * frameSize;

                    if ( pprm.rlOffset > sample->bufsz )
                        pprm.rlOffset = sample->bufsz;

                    if ( !pprm.rlSmplCnt )
                        pprm.rlSmplCnt = sample->bufsz;

                    if ( pprm.rlSmplCnt + pprm.rlOffset > sample->bufsz )
                        pprm.rlSmplCnt = sample->bufsz - pprm.rlOffset;
                }
                else
                {
                    ypa_log_out("Warning: Could not load sample %s.\n", ExtSamples.at(i).Name.c_str());
                }
            }
        }
        else if ( !MainSample.Name.empty() )
        {
            MainSample.Sample = Nucleus::CInit<NC_STACK_wav>( {{NC_STACK_rsrc::RSRC_ATT_NAME, MainSample.Name}} );

            if ( !MainSample.Sample )
                ypa_log_out("Warning: Could not load sample %s.\n", MainSample.Name.c_str());
        }

        if ( extS.empty() )
        {
            for (TSndSample &sample : MainSampleVariants)
            {
                if ( !sample.Name.empty() )
                {
                    sample.Sample = Nucleus::CInit<NC_STACK_wav>( {{NC_STACK_rsrc::RSRC_ATT_NAME, sample.Name}} );

                    if ( !sample.Sample )
                        ypa_log_out("Warning: Could not load sample %s.\n", sample.Name.c_str());
                }
            }
        }

        Common::Env.SetPrefix("rsrc", oldRsrc);
    }
}

void TVhclSound::SetMainSampleVariant(size_t variant, const std::string &name)
{
    if ( variant == 0 )
    {
        MainSample.Name = name;
        return;
    }

    if ( MainSampleVariants.size() < variant )
        MainSampleVariants.resize(variant);

    MainSampleVariants.at(variant - 1).Name = name;
}

void TVhclSound::ClearSounds()
{
    MainSample.ClearLoaded();

    for (TSndSample &sample : MainSampleVariants)
        sample.ClearLoaded();

    for (TSndSample &sample : ExtSamples)
        sample.ClearLoaded();

    for (TSampleParams &fragment : extS)
    {
        fragment.Sample = NULL;
        fragment.rlOffset = 0;
        fragment.rlSmplCnt = 0;
    }
}

    
TVhclProto::~TVhclProto()
{
    if ( wireframe )
    {
        wireframe->Delete();
        wireframe = NULL;
    }

    if ( hud_wireframe )
    {
        hud_wireframe->Delete();
        hud_wireframe = NULL;
    }

    if ( mg_wireframe )
    {
        mg_wireframe->Delete();
        mg_wireframe = NULL;
    }

    if ( wpn_wireframe_1 )
    {
        wpn_wireframe_1->Delete();
        wpn_wireframe_1 = NULL;
    }

    if ( wpn_wireframe_2 )
    {
        wpn_wireframe_2->Delete();
        wpn_wireframe_2 = NULL;
    }
    
    Common::DeleteAndNull(&RoboProto);
}

TWeapProto::~TWeapProto()
{
    if ( wireframe )
    {
        wireframe->Delete();
        wireframe = NULL;
    }
}
    
}
