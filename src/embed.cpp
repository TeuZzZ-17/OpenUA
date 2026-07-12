#include "includes.h"
#include "nucleas.h"

#include "embed.h"
#include "utils.h"

#include "IFFile.h"
#include "rsrc.h"

#include "math.h"
#include "inttypes.h"

namespace
{
bool IsSetLooseEmrsOverrideClass(const std::string &classname)
{
    return classname == "ilbm.class" ||
           classname == "sklt.class" ||
           classname == "bmpanim.class";
}

bool SkipCurrentOrNextPayload(IFFile *mfile, int payloadParse)
{
    if ( payloadParse == IFFile::IFF_ERR_EOC )
    {
        if ( !mfile->parse() )
            return mfile->skipChunk();
        return false;
    }

    if ( !payloadParse )
        return mfile->skipChunk();

    return false;
}
}

// Create embed resource node and fill rsrc field data
size_t NC_STACK_embed::Init(IDVList &)
{
    dprintf("MAKE ME %s\n","embed_func0");
    return 0;
}

size_t NC_STACK_embed::Deinit()
{
    for ( NC_STACK_rsrc *res : _resources )
        res->Delete();

    _resources.clear();

    return NC_STACK_nucleus::Deinit();
}

size_t NC_STACK_embed::LoadingFromIFF(IFFile **file)
{
    IFFile *mfile = *file;
    int obj_ok = 0;

    while ( 1 )
    {
        int v5 = mfile->parse();

        if ( v5 == -2 )
            break;

        if ( v5 )
        {
            if ( obj_ok )
                Deinit();
            return 0;
        }

        const IFFile::Context &chunk = mfile->GetCurrentChunk();

        if ( chunk.Is(TAG_FORM, TAG_ROOT) )
        {
            obj_ok = NC_STACK_nucleus::LoadingFromIFF(file);

            if ( !obj_ok )
                return 0;

            _resources.clear();
        }
        else if ( chunk.Is(TAG_EMRS) )
        {
            std::string classname = mfile->readStr(255);
            size_t emrsOffset = mfile->tell();
            int payloadParse = mfile->parse();

            std::string resname;
            size_t fnd = classname.find('\0');
            if (fnd != std::string::npos)
            {
                resname = classname.substr(fnd + 1);
                classname.resize(fnd);
            }
            if (!classname.empty() && classname.back() == '\0')
                classname.pop_back();
            if (!resname.empty() && resname.back() == '\0')
                resname.pop_back();

            if ( IsSetLooseEmrsOverrideClass(classname) )
            {
                std::string payload = "unknown";
                if ( payloadParse == IFFile::IFF_ERR_EOC )
                    payload = mfile->PeekNextChunkLabel();
                else if ( !payloadParse )
                    payload = IFFile::ChunkLabel(mfile->GetCurrentChunk());

                if ( IFFile::IsSkyLooseScopeActive() )
                {
                    if ( classname == "ilbm.class" )
                    {
                        IFFile::SetLooseOverride skyPngOverrideInfo;
                        if ( IFFile::FindSkyLooseEmrsPngOverride(resname,
                                                                  "rb",
                                                                  classname,
                                                                  payload,
                                                                  &skyPngOverrideInfo,
                                                                  "NC_STACK_embed::LoadingFromIFF",
                                                                  emrsOffset) )
                        {
                            NC_STACK_rsrc *png_class = Nucleus::CTFInit<NC_STACK_rsrc>(classname,
                               {{NC_STACK_rsrc::RSRC_ATT_NAME, resname},
                                {NC_STACK_rsrc::RSRC_ATT_TRYSHARED, (int32_t)1},
                                {NC_STACK_rsrc::RSRC_ATT_SET_LOOSE_PNG_PATH, skyPngOverrideInfo.resolvedPath},
                                {NC_STACK_rsrc::RSRC_ATT_SKIP_SET_LOOSE_OVERRIDE, (int32_t)1}});

                            if ( png_class )
                            {
                                if ( SkipCurrentOrNextPayload(mfile, payloadParse) )
                                {
                                    ypa_log_out("OpenUA sky loose PNG override used: %s -> %s\n", resname.c_str(), skyPngOverrideInfo.resolvedPath.c_str());
                                    _resources.push_back(png_class);
                                    continue;
                                }

                                png_class->Delete();
                                ypa_log_out("WARNING: OpenUA sky loose PNG override failed for %s (%s); falling back to embedded BAS payload.\n", resname.c_str(), skyPngOverrideInfo.resolvedPath.c_str());
                            }
                            else
                            {
                                ypa_log_out("WARNING: OpenUA sky loose PNG override failed for %s (%s); falling back to embedded BAS payload.\n", resname.c_str(), skyPngOverrideInfo.resolvedPath.c_str());
                            }
                        }
                    }

                    IFFile::SetLooseOverride skyOverrideInfo;
                    if ( IFFile::FindSkyLooseEmrsOverride(resname,
                                                          "rb",
                                                          classname,
                                                          payload,
                                                          &skyOverrideInfo,
                                                          "NC_STACK_embed::LoadingFromIFF",
                                                          emrsOffset) )
                    {
                        FSMgr::FileHandle looseHandle = FSMgr::iDir::openFile(skyOverrideInfo.resolvedPath, "rb");
                        if ( looseHandle.OK() )
                        {
                            IFFile looseFile(std::move(looseHandle));
                            NC_STACK_rsrc *override_class = Nucleus::CTFInit<NC_STACK_rsrc>(classname,
                               {{NC_STACK_rsrc::RSRC_ATT_NAME, resname},
                                {NC_STACK_rsrc::RSRC_ATT_TRYSHARED, (int32_t)1},
                                {NC_STACK_rsrc::RSRC_ATT_PIFFFILE, &looseFile},
                                {NC_STACK_rsrc::RSRC_ATT_SKIP_SET_LOOSE_OVERRIDE, (int32_t)1}});

                            if ( override_class )
                            {
                                bool payloadSkipped = SkipCurrentOrNextPayload(mfile, payloadParse);

                                if ( payloadSkipped )
                                {
                                    ypa_log_out("OpenUA sky loose embedded override used: %s -> %s\n", resname.c_str(), skyOverrideInfo.resolvedPath.c_str());
                                    _resources.push_back(override_class);
                                    continue;
                                }

                                override_class->Delete();
                                ypa_log_out("WARNING: OpenUA sky loose embedded override failed for %s (%s); embedded payload skip failed, falling back.\n", resname.c_str(), skyOverrideInfo.resolvedPath.c_str());
                            }
                            else
                            {
                                ypa_log_out("WARNING: OpenUA sky loose embedded override failed for %s (%s); falling back to embedded BAS payload.\n", resname.c_str(), skyOverrideInfo.resolvedPath.c_str());
                            }
                        }
                        else
                        {
                            ypa_log_out("WARNING: OpenUA sky loose embedded override failed for %s (%s); failed to open, falling back.\n", resname.c_str(), skyOverrideInfo.resolvedPath.c_str());
                        }
                    }
                }

                if ( classname == "ilbm.class" )
                {
                    IFFile::SetLooseOverride pngOverrideInfo;
                    if ( IFFile::FindSetLooseEmrsPngOverride(resname,
                                                              "rb",
                                                              classname,
                                                              payload,
                                                              &pngOverrideInfo,
                                                              "NC_STACK_embed::LoadingFromIFF",
                                                              emrsOffset) )
                    {
                        NC_STACK_rsrc *png_class = Nucleus::CTFInit<NC_STACK_rsrc>(classname,
                           {{NC_STACK_rsrc::RSRC_ATT_NAME, resname},
                            {NC_STACK_rsrc::RSRC_ATT_TRYSHARED, (int32_t)1},
                            {NC_STACK_rsrc::RSRC_ATT_SET_LOOSE_PNG_PATH, pngOverrideInfo.resolvedPath},
                            {NC_STACK_rsrc::RSRC_ATT_SKIP_SET_LOOSE_OVERRIDE, (int32_t)1}});

                        if ( png_class )
                        {
                            if ( SkipCurrentOrNextPayload(mfile, payloadParse) )
                            {
                                IFFile::ReportSetLooseOverrideUsed(pngOverrideInfo);
                                ypa_log_out("OpenUA SET loose PNG override used: %s -> %s\n", resname.c_str(), pngOverrideInfo.resolvedPath.c_str());
                                _resources.push_back(png_class);
                                continue;
                            }

                            png_class->Delete();
                            IFFile::ReportSetLooseOverrideFailed(pngOverrideInfo, "PNG override loaded but embedded payload skip failed; embedded payload fallback used.");
                            ypa_log_out("WARNING: OpenUA SET loose PNG override failed for %s (%s); falling back to ILBM/embedded payload.\n", resname.c_str(), pngOverrideInfo.resolvedPath.c_str());
                        }
                        else
                        {
                            IFFile::ReportSetLooseOverrideFailed(pngOverrideInfo, "PNG override existed but failed to load; ILBM/embedded payload fallback used.");
                            ypa_log_out("WARNING: OpenUA SET loose PNG override failed for %s (%s); falling back to ILBM/embedded payload.\n", resname.c_str(), pngOverrideInfo.resolvedPath.c_str());
                        }
                    }
                }

                IFFile::SetLooseOverride overrideInfo;
                if ( IFFile::FindSetLooseEmrsOverride(resname,
                                                       "rb",
                                                       classname,
                                                       payload,
                                                       &overrideInfo,
                                                       "NC_STACK_embed::LoadingFromIFF",
                                                       emrsOffset) )
                {
                    overrideInfo.embeddedPayload = payload;

                    FSMgr::FileHandle looseHandle = FSMgr::iDir::openFile(overrideInfo.resolvedPath, "rb");
                    if ( looseHandle.OK() )
                    {
                        IFFile looseFile(std::move(looseHandle));
                        NC_STACK_rsrc *override_class = Nucleus::CTFInit<NC_STACK_rsrc>(classname,
                           {{NC_STACK_rsrc::RSRC_ATT_NAME, resname},
                            {NC_STACK_rsrc::RSRC_ATT_TRYSHARED, (int32_t)1},
                            {NC_STACK_rsrc::RSRC_ATT_PIFFFILE, &looseFile},
                            {NC_STACK_rsrc::RSRC_ATT_SKIP_SET_LOOSE_OVERRIDE, (int32_t)1}});

                        if ( override_class )
                        {
                            bool payloadSkipped = SkipCurrentOrNextPayload(mfile, payloadParse);

                            if ( payloadSkipped )
                            {
                                IFFile::ReportSetLooseOverrideUsed(overrideInfo);

                                _resources.push_back(override_class);
                                continue;
                            }

                            override_class->Delete();
                            IFFile::ReportSetLooseOverrideFailed(overrideInfo, "loose EMRS override loaded but embedded payload skip failed; embedded payload fallback used.");
                        }

                        IFFile::ReportSetLooseOverrideFailed(overrideInfo, "loose EMRS override existed but failed to load; embedded payload fallback used.");
                    }
                    else
                    {
                        IFFile::ReportSetLooseOverrideFailed(overrideInfo, "loose EMRS override existed but failed to open; embedded payload fallback used.");
                    }
                }
            }

            NC_STACK_rsrc *embd_class = Nucleus::CTFInit<NC_STACK_rsrc>(classname,
               {{NC_STACK_rsrc::RSRC_ATT_NAME, resname},
                {NC_STACK_rsrc::RSRC_ATT_TRYSHARED, (int32_t)1},
                {NC_STACK_rsrc::RSRC_ATT_PIFFFILE, mfile}});

            if ( !embd_class )
            {
                Deinit();
                return 0;
            }

            _resources.push_back(embd_class);
        }
        else
        {
            mfile->skipChunk();
        }

    }
    return obj_ok;
}

size_t NC_STACK_embed::SavingIntoIFF(IFFile **file)
{
    IFFile *mfile = *file;

    if ( mfile->pushChunk(TAG_EMBD, TAG_FORM, -1) )
        return 0;

    if ( !NC_STACK_nucleus::SavingIntoIFF(file) )
        return 0;

    for ( NC_STACK_rsrc *embd_obj : _resources )
    {
        if ( embd_obj )
        {
            std::string classname = embd_obj->ClassName();
            std::string resname = embd_obj->getRsrc_name();

            mfile->pushChunk(0, TAG_EMRS, -1);
            mfile->write(classname.c_str(), classname.length() + 1);
            mfile->write(resname.c_str(), resname.length() + 1);
            mfile->writeU8(0);
            mfile->popChunk();

            rsrc_func66_arg arg66;
            arg66.filename = NULL;
            arg66.file = mfile;
            arg66.OpenedStream = 2;

            if ( !embd_obj->rsrc_func66(&arg66 ) )
                return 0;
        }
    }

    return mfile->popChunk() == IFFile::IFF_ERR_OK;
}
