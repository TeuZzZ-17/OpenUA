#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#include "includes.h"
#include "nucleas.h"
#include "base.h"
#include "utils.h"

namespace
{
thread_local std::vector<std::string> g_activeSetLooseBaseObjects;

std::string SetLooseBaseObjectKey(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return name;
}

bool IsSetLooseBaseObjectActive(const std::string &name)
{
    const std::string key = SetLooseBaseObjectKey(name);
    return std::find(g_activeSetLooseBaseObjects.begin(), g_activeSetLooseBaseObjects.end(), key) !=
           g_activeSetLooseBaseObjects.end();
}

class SetLooseBaseObjectGuard
{
public:
    explicit SetLooseBaseObjectGuard(const std::string &name) : _key(SetLooseBaseObjectKey(name))
    {
        g_activeSetLooseBaseObjects.push_back(_key);
    }

    ~SetLooseBaseObjectGuard()
    {
        if (!g_activeSetLooseBaseObjects.empty())
            g_activeSetLooseBaseObjects.pop_back();
    }

private:
    std::string _key;
};

NC_STACK_base *LoadLooseBaseObjectFile(IFFile *mfile)
{
    if ( !mfile || mfile->parse() )
        return NULL;

    if ( !mfile->GetCurrentChunk().Is(TAG_FORM, TAG_MC2) || mfile->parse() )
        return NULL;

    if ( !mfile->GetCurrentChunk().Is(TAG_FORM, TAG_OBJT) )
        return NULL;

    return dynamic_cast<NC_STACK_base *>(NC_STACK_nucleus::LoadObjectFromIFF(mfile));
}

NC_STACK_base *TryLoadSetLooseBaseObject(const std::string &objectName)
{
    if ( objectName.empty() || IsSetLooseBaseObjectActive(objectName) ||
         !IFFile::IsSetLooseBaseObjectScopeActive() )
        return NULL;

    IFFile::SetLooseOverride overrideInfo;
    if ( !IFFile::FindSetLooseBaseObjectOverride(objectName,
                                                  "rb",
                                                  &overrideInfo,
                                                  "NC_STACK_nucleus::LoadObjectFromIFF") )
        return NULL;

    FSMgr::FileHandle looseHandle = FSMgr::iDir::openFile(overrideInfo.resolvedPath, "rb");
    if ( !looseHandle.OK() )
    {
        IFFile::ReportSetLooseOverrideFailed(overrideInfo, "loose BASE object existed but failed to open; embedded SET.BAS object retained.");
        ypa_log_out("WARNING: OpenUA SET loose BASE override failed to open for %s (%s); embedded object retained.\n",
                    objectName.c_str(), overrideInfo.resolvedPath.c_str());
        return NULL;
    }

    IFFile looseFile(std::move(looseHandle));
    SetLooseBaseObjectGuard guard(objectName);
    NC_STACK_base *overrideObject = LoadLooseBaseObjectFile(&looseFile);

    if ( !overrideObject )
    {
        IFFile::ReportSetLooseOverrideFailed(overrideInfo, "loose BASE object failed to parse; embedded SET.BAS object retained.");
        ypa_log_out("WARNING: OpenUA SET loose BASE override failed to parse for %s (%s); embedded object retained.\n",
                    objectName.c_str(), overrideInfo.resolvedPath.c_str());
        return NULL;
    }

    if ( StriCmp(overrideObject->getName(), objectName) )
    {
        const std::string loadedName = overrideObject->getName();
        overrideObject->Delete();
        IFFile::ReportSetLooseOverrideFailed(overrideInfo, "loose BASE object NAME does not match the embedded object; embedded SET.BAS object retained.");
        ypa_log_out("WARNING: OpenUA SET loose BASE override name mismatch: expected %s, loaded %s (%s); embedded object retained.\n",
                    objectName.c_str(), loadedName.c_str(), overrideInfo.resolvedPath.c_str());
        return NULL;
    }

    IFFile::ReportSetLooseOverrideUsed(overrideInfo);
    ypa_log_out("OpenUA SET loose BASE override used: %s -> %s\n",
                objectName.c_str(), overrideInfo.resolvedPath.c_str());
    return overrideObject;
}
}

namespace Nucleus
{
TClassList TClassList::Instance;

TClassList::iterator TClassList::find(const std::string &clsname)
{
    for(iterator it = begin(); it != end(); it++)
    {
        if (!StriCmp(clsname, it->_classname) )
            return it;
    }

    return end();
}





NC_STACK_nucleus *CFInit(const std::string &classname, IDVList &stak)
{
    TClassList::iterator it = ClassList.find(classname);
    if (it == ClassList.end())
        return NULL;

    NC_STACK_nucleus *inst = it->_newinstance();

    if ( !inst->Init(stak) )
    {
        delete inst;
        return NULL;
    }

    return inst;
}

NC_STACK_nucleus *CFInit(const std::string &classname)
{
    IDVList empty;
    return CFInit(classname, empty);
}

NC_STACK_nucleus *CFInit(const std::string &classname, IDVList::TInitList lst)
{
    IDVList stak(lst);
    return CFInit(classname, stak);
}

}


size_t NC_STACK_nucleus::Init(IDVList &stak)
{
    for( auto& it : stak )
    {
        IDVPair &val = it.second;

        if ( !val.Skip )
        {
            switch (val.ID)
            {
            case NC_ATT_NAME:
                setName( val.Get<std::string>() );
                break;

            default:
                break;
            }
        }
    }

    return 1;
}

size_t NC_STACK_nucleus::Deinit()
{
    NAME.clear();

    return 1;
}

size_t NC_STACK_nucleus::LoadingFromIFF(IFFile **file)
{
    IFFile *mfile = *file;

    while ( 1 )
    {
        signed int v6 = mfile->parse();
        if ( v6 == IFFile::IFF_ERR_EOC )
            break;
        if ( v6 )
        {
            Deinit();
            return 0;
        }
        if ( mfile->GetCurrentChunk().Is(TAG_NAME) )
        {
            std::string name = mfile->readStr(32);
            if ( name.empty() )
            {
                Deinit();
                return 0;
            }

            setName(name);
            mfile->parse();
        }
        else
        {
            mfile->skipChunk();
        }
    }

    return 1;
}


size_t NC_STACK_nucleus::SavingIntoIFF(IFFile **val)
{
    IFFile *mfile = *val;
    if ( mfile->pushChunk(TAG_ROOT, TAG_FORM, -1) )
        return 0;
    else
    {
        if ( !NAME.empty() && NAME.length() <= 32 )
        {
            mfile->pushChunk(0, TAG_NAME, -1);
            mfile->write(NAME.c_str(), NAME.length());
            mfile->popChunk();
        }
        mfile->popChunk();
        return 1;
    }
    return 0;
}

void NC_STACK_nucleus::setName(const std::string &newName)
{
    if ( !newName.empty() )
        NAME = newName;
    else
        NAME.clear();
}

const std::string &NC_STACK_nucleus::getName() const
{
    return NAME;
}



NC_STACK_nucleus *NC_STACK_nucleus::LoadObjectFromIFF(IFFile *mfile)
{
    Nucleus::TClassList::iterator clss = Nucleus::ClassList.end();
    NC_STACK_nucleus *obj = NULL;
    while ( 1 )
    {
        int v4 = mfile->parse();

        if ( v4 == IFFile::IFF_ERR_EOC )
            break;

        if ( v4 )
            return NULL;

        const IFFile::Context &chunk = mfile->GetCurrentChunk();

        if ( chunk.Is(TAG_CLID) )
        {
            std::string classname = mfile->readStr(256);

            if ( classname.empty() )
                return NULL;

            clss = Nucleus::ClassList.find(classname);
            if ( clss == Nucleus::ClassList.end() )
                return NULL;


            mfile->parse();
        }
        else if ( chunk.Is(TAG_FORM) )
        {
            IFFile *v11 = mfile;

            obj = clss->_newinstance();

            if ( !obj->LoadingFromIFF(&v11) )
            {
                delete obj;
                return NULL;
            }

            if ( obj && !StriCmp(clss->_classname, NC_STACK_base::__ClassName) )
            {
                NC_STACK_base *embeddedBase = dynamic_cast<NC_STACK_base *>(obj);
                NC_STACK_base *looseBase = embeddedBase ? TryLoadSetLooseBaseObject(embeddedBase->getName()) : NULL;

                if ( looseBase )
                {
                    obj->Delete();
                    obj = looseBase;
                }
            }
        }
        else
        {
            if ( !mfile->skipChunk() )
            {
                return NULL;
            }

        }
    }
    return obj;
}

bool NC_STACK_nucleus::SaveObjectIntoIFF(IFFile *mfile)
{
    if ( mfile->pushChunk(TAG_OBJT, TAG_FORM, -1) )
        return 0;

    std::string clsname = ClassName();

    if ( mfile->pushChunk(0, TAG_CLID, clsname.length() + 1) )
        return 0;

    if ( mfile->write(clsname.c_str(), clsname.length() + 1) < 0 )
        return 0;

    mfile->popChunk();

    IFFile *tmp = mfile;
    int res = SavingIntoIFF(&tmp);
    mfile->popChunk();

    return res;
}
