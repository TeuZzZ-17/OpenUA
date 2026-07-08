#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string.h>
#include <utility>
#include <vector>
#include "includes.h"
#include "IFFile.h"
#include "utils.h"
#include "env.h"

namespace
{
struct SetLooseReportEntry
{
    std::string requested;
    std::string resolvedPath;
    std::string extensionForm;
    std::string reason;
    bool embedded = false;
    std::string sourceFunction;
    bool emrs = false;
    std::string emrsClass;
    std::string embeddedPayload;
};

struct SetLooseLookupEntry
{
    std::string requested;
    std::string originalCandidate;
    bool originalExists = false;
    std::string legacyCandidate;
    bool legacyExists = false;
    std::string sourceFunction;
};

struct SetLooseEmrsLookupEntry
{
    std::string requested;
    std::string className;
    std::string payload;
    std::string currentOffset;
    std::string originalCandidate;
    bool originalExists = false;
    std::string legacyCandidate;
    bool legacyExists = false;
    std::string sourceFunction;
};

struct SetBasMarkerEntry
{
    std::string name;
    std::vector<size_t> offsets;
};

struct SetBasRawScanEntry
{
    std::string openedAsset;
    std::string resolvedSource;
    std::string diskPath;
    std::string sourceKind;
    size_t fileSize = 0;
    std::string rawStatus;
    std::string firstBytesHex;
    std::string firstFormType;
    std::vector<std::string> topLevelChunks;
    std::vector<SetBasMarkerEntry> markers;
    std::string sourceFunction;
};

struct SetLooseReport
{
    bool initialized = false;
    bool available = false;
    int32_t setId = 0;
    std::string root;
    std::string reportPath;
    std::vector<SetLooseReportEntry> used;
    std::vector<SetLooseReportEntry> failed;
    std::vector<SetLooseLookupEntry> lookups;
    std::vector<SetLooseLookupEntry> embeddedLookups;
    std::vector<SetLooseEmrsLookupEntry> emrsLookups;
    std::vector<SetBasRawScanEntry> setBasRawScans;
    std::vector<std::string> setBasParseTrace;
    size_t emrsResourcesChecked = 0;
    std::set<std::string> usedKeys;
    std::set<std::string> failedKeys;
    std::set<std::string> lookupKeys;
    std::set<std::string> embeddedLookupKeys;
    std::set<std::string> emrsLookupKeys;
    std::set<std::string> setBasRawScanKeys;
};

std::map<int32_t, SetLooseReport> g_setLooseReports;
bool g_setBasParseTraceActive = false;
int32_t g_setBasParseTraceSetId = 0;
size_t g_setBasParseTraceCount = 0;
const size_t SETBAS_PARSE_TRACE_LIMIT = 200;

std::string setLooseNormalizeSlashes(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string setLooseLower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return str;
}

std::string setLooseTagToString(uint32_t tag)
{
    char name[5] = {
        (char)((tag >> 24) & 0xFF),
        (char)((tag >> 16) & 0xFF),
        (char)((tag >> 8) & 0xFF),
        (char)(tag & 0xFF),
        0
    };
    return std::string(name);
}

std::string setLooseChunkLabel(const IFFile::Context &chunk)
{
    std::string label = setLooseTagToString(chunk.TAG);
    if (chunk.TAG == TAG_FORM)
        label += " " + setLooseTagToString(chunk.TAG_EXTENSION);
    return label;
}

std::string setLooseFormatOffset(size_t offset)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%zX", offset);
    return std::string(buf);
}

std::string setLooseFormatOffsets(const std::vector<size_t> &offsets)
{
    if (offsets.empty())
        return std::string();

    std::string result;
    for (size_t i = 0; i < offsets.size(); i++)
    {
        if (i)
            result += ", ";
        result += setLooseFormatOffset(offsets[i]);
    }
    return result;
}

uint32_t setLooseReadU32BE(const std::vector<uint8_t> &data, size_t offset)
{
    if (offset + 4 > data.size())
        return 0;

    return ((uint32_t)data[offset] << 24) |
           ((uint32_t)data[offset + 1] << 16) |
           ((uint32_t)data[offset + 2] << 8) |
           (uint32_t)data[offset + 3];
}

std::vector<size_t> setLooseFindMarkerOffsets(const std::vector<uint8_t> &data, const std::string &marker)
{
    std::vector<size_t> offsets;
    if (marker.empty() || data.size() < marker.size())
        return offsets;

    for (size_t pos = 0; pos + marker.size() <= data.size(); pos++)
    {
        if (memcmp(data.data() + pos, marker.data(), marker.size()) == 0)
        {
            offsets.push_back(pos);
            if (offsets.size() >= 8)
                break;
        }
    }
    return offsets;
}

std::string setLooseFirstBytesHex(const std::vector<uint8_t> &data)
{
    size_t count = std::min<size_t>(64, data.size());
    std::string result;

    for (size_t i = 0; i < count; i++)
    {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        if (i)
            result += " ";
        result += buf;
    }
    return result;
}

std::vector<std::string> setLooseTopLevelChunks(const std::vector<uint8_t> &data)
{
    std::vector<std::string> chunks;
    if (data.size() < 12 || setLooseReadU32BE(data, 0) != TAG_FORM)
        return chunks;

    size_t offset = 12;
    while (offset + 8 <= data.size() && chunks.size() < 12)
    {
        uint32_t tag = setLooseReadU32BE(data, offset);
        uint32_t size = setLooseReadU32BE(data, offset + 4);

        std::string label = setLooseTagToString(tag);
        if (tag == TAG_FORM && offset + 12 <= data.size())
            label += " " + setLooseTagToString(setLooseReadU32BE(data, offset + 8));

        label += " @ " + setLooseFormatOffset(offset);
        chunks.push_back(label);

        size_t advance = 8 + size + (size & 1);
        if (!advance || offset + advance <= offset)
            break;
        offset += advance;
    }
    return chunks;
}

std::string setLooseStripLeadingSlashes(std::string path)
{
    while (!path.empty() && (path.front() == '/' || path.front() == '\\'))
        path.erase(path.begin());
    return path;
}

std::string setLooseTrimResourceName(std::string path)
{
    while (!path.empty() && (unsigned char)path.front() <= 32)
        path.erase(path.begin());

    while (!path.empty() && (unsigned char)path.back() <= 32)
        path.pop_back();

    return path;
}

bool setLooseIsReadMode(const std::string &mode)
{
    return mode.find('r') != std::string::npos &&
           mode.find('w') == std::string::npos &&
           mode.find('a') == std::string::npos;
}

int32_t setLooseCurrentSetId()
{
    std::string prefix = setLooseLower(setLooseNormalizeSlashes(Common::Env.GetPrefix("rsrc")));
    const std::string tag = "data:set";

    if (prefix.compare(0, tag.size(), tag) != 0)
        return 0;

    size_t pos = tag.size();
    int32_t setId = 0;
    bool hasDigit = false;

    while (pos < prefix.size() && std::isdigit((unsigned char)prefix[pos]))
    {
        hasDigit = true;
        setId = setId * 10 + (prefix[pos] - '0');
        pos++;
    }

    if (!hasDigit || setId < 1 || setId > 6)
        return 0;

    return setId;
}

std::string setLooseAssetFromRequest(const std::string &filename, int32_t setId)
{
    std::string request = setLooseNormalizeSlashes(filename);
    size_t colonPos = request.find(':');

    if (colonPos != std::string::npos)
    {
        std::string prefix = setLooseLower(request.substr(0, colonPos));
        if (prefix == "rsrc")
            return setLooseStripLeadingSlashes(request.substr(colonPos + 1));
    }

    std::string lowered = setLooseLower(request);
    std::string setTag = "data:set" + std::to_string(setId);

    if (lowered.compare(0, setTag.size(), setTag) == 0)
    {
        size_t pos = setTag.size();
        if (pos < request.size() && (request[pos] == ':' || request[pos] == '/' || request[pos] == '\\'))
            pos++;

        return setLooseStripLeadingSlashes(request.substr(pos));
    }

    return std::string();
}

std::string setLooseExtension(const std::string &path)
{
    size_t slashPos = path.find_last_of("/\\");
    size_t dotPos = path.rfind('.');

    if (dotPos == std::string::npos || (slashPos != std::string::npos && dotPos < slashPos))
        return std::string();

    return setLooseLower(path.substr(dotPos + 1));
}

std::string setLooseReplaceExtension(const std::string &path, const std::string &newExt)
{
    size_t slashPos = path.find_last_of("/\\");
    size_t dotPos = path.rfind('.');

    if (dotPos == std::string::npos || (slashPos != std::string::npos && dotPos < slashPos))
        return path + newExt;

    return path.substr(0, dotPos) + newExt;
}

std::string setLooseFileName(const std::string &path)
{
    size_t slashPos = path.find_last_of("/\\");
    if (slashPos == std::string::npos)
        return path;
    return path.substr(slashPos + 1);
}

std::string setLooseDirName(const std::string &path)
{
    size_t slashPos = path.find_last_of("/\\");
    if (slashPos == std::string::npos)
        return std::string();
    return path.substr(0, slashPos);
}

bool setLooseCanOpenDiskFile(const std::string &path)
{
    FSMgr::FileHandle fil(path, "rb");
    return fil.OK();
}

bool setLooseResolveReadableFile(const std::string &virtualPath, std::string *openPath)
{
    if ( FSMgr::iDir::fileExist(virtualPath) )
    {
        if (openPath)
            *openPath = virtualPath;
        return true;
    }

    std::string dirName = setLooseDirName(virtualPath);
    std::string fileName = setLooseFileName(virtualPath);
    if ( dirName.empty() || fileName.empty() )
        return false;

    FSMgr::iNode *dirNode = FSMgr::iDir::findNode(dirName);
    if ( !dirNode || dirNode->getType() != FSMgr::iNode::NTYPE_DIR )
        return false;

    std::string diskPath = setLooseNormalizeSlashes(dirNode->getPath());
    if ( !diskPath.empty() && diskPath.back() != '/' )
        diskPath += "/";
    diskPath += fileName;

    if ( !setLooseCanOpenDiskFile(diskPath) )
        return false;

    if (openPath)
        *openPath = diskPath;
    return true;
}

bool setLooseSupportedExtension(const std::string &assetPath)
{
    std::string ext = setLooseExtension(assetPath);

    return ext == "base" || ext == "bas" ||
           ext == "sklt" || ext == "skl" ||
           ext == "ilbm" || ext == "ilb" ||
           ext == "anm";
}

bool setLooseWriteReport(SetLooseReport &report)
{
    if (!report.available)
        return false;

    FSMgr::FileHandle *fil = FSMgr::iDir::openFileAlloc(report.reportPath, "w");
    if (!fil)
        return false;

    fil->puts("OpenUA SET Loose Override Report\n");
    fil->printf("Set: %d\n", report.setId);
    fil->printf("Loose root: %s\n\n", report.root.c_str());

    fil->puts("USED:\n");
    if (report.used.empty())
        fil->puts("<none>\n");
    else
    {
        for (const SetLooseReportEntry &entry : report.used)
        {
            fil->printf("%s [%s] -> %s\n",
                        entry.requested.c_str(),
                        entry.extensionForm.c_str(),
                        entry.resolvedPath.c_str());
            if (entry.emrs)
            {
                fil->puts("  source: EMRS embedded SET resource override\n");
                fil->printf("  class: %s\n", entry.emrsClass.c_str());
                fil->printf("  embedded payload replaced: %s\n", entry.embeddedPayload.c_str());
            }
            else if (entry.embedded)
            {
                fil->puts("  source: embedded SET resource override\n");
                fil->printf("  loader: %s\n", entry.sourceFunction.c_str());
            }
        }
    }

    fil->puts("\nFAILED OVERRIDES:\n");
    if (report.failed.empty())
        fil->puts("<none>\n");
    else
    {
        for (const SetLooseReportEntry &entry : report.failed)
        {
            fil->printf("%s [%s] -> %s\n",
                        entry.requested.c_str(),
                        entry.extensionForm.c_str(),
                        entry.resolvedPath.c_str());
            fil->printf("Reason: %s\n", entry.reason.c_str());
            if (entry.emrs)
            {
                fil->puts("  source: EMRS embedded SET resource override\n");
                fil->printf("  class: %s\n", entry.emrsClass.c_str());
                fil->printf("  embedded payload retained: %s\n", entry.embeddedPayload.c_str());
            }
            else if (entry.embedded)
            {
                fil->puts("  source: embedded SET resource override\n");
                fil->printf("  loader: %s\n", entry.sourceFunction.c_str());
            }
        }
    }

    fil->puts("\nLOOKUPS:\n");
    if (report.lookups.empty())
        fil->puts("<none>\n");
    else
    {
        for (const SetLooseLookupEntry &entry : report.lookups)
        {
            fil->printf("%s\n", entry.requested.c_str());
            fil->printf("  original candidate: %s [exists %s]\n",
                        entry.originalCandidate.c_str(),
                        entry.originalExists ? "yes" : "no");
            fil->printf("  legacy candidate:   %s [exists %s]\n",
                        entry.legacyCandidate.c_str(),
                        entry.legacyExists ? "yes" : "no");
            fil->printf("  source function: %s\n", entry.sourceFunction.c_str());
        }
    }

    fil->puts("\nSET LOOSE OVERRIDE SUMMARY:\n");
    fil->printf("EMRS resources checked: %zu\n", report.emrsResourcesChecked);
    fil->printf("overrides used: %zu\n", report.used.size());
    fil->printf("failed overrides: %zu\n", report.failed.size());

    delete fil;
    return true;
}

bool setLooseEnsureReport(int32_t setId)
{
    SetLooseReport &report = g_setLooseReports[setId];

    if (!report.initialized)
    {
        report.initialized = true;
        report.setId = setId;
        report.root = "Data/Set" + std::to_string(setId) + "/Loose/";
        report.reportPath = report.root + "_openua_set_override_report.txt";

        FSMgr::iNode *rootNode = FSMgr::iDir::findNode(report.root);
        report.available = rootNode && rootNode->getType() == FSMgr::iNode::NTYPE_DIR;

        if (report.available)
            setLooseWriteReport(report);
    }

    return report.available;
}

void setLooseAddUsed(const IFFile::SetLooseOverride &overrideInfo)
{
    if (!overrideInfo.active)
        return;

    if (overrideInfo.setId == 0 && overrideInfo.extensionForm.find("sky loose archive") != std::string::npos)
    {
        ypa_log_out("OpenUA sky loose archive override used: %s -> %s\n", overrideInfo.requested.c_str(), overrideInfo.resolvedPath.c_str());
        return;
    }

    if (!setLooseEnsureReport(overrideInfo.setId))
        return;

    SetLooseReport &report = g_setLooseReports[overrideInfo.setId];
    std::string key = overrideInfo.requested + "\n" + overrideInfo.resolvedPath + "\n" +
                      (overrideInfo.emrs ? "emrs" : (overrideInfo.embedded ? "embedded" : "file")) + "\n" +
                      overrideInfo.sourceFunction;

    if (!report.usedKeys.insert(key).second)
        return;

    report.used.push_back({overrideInfo.requested,
                           overrideInfo.resolvedPath,
                           overrideInfo.extensionForm,
                           std::string(),
                           overrideInfo.embedded,
                           overrideInfo.sourceFunction,
                           overrideInfo.emrs,
                           overrideInfo.emrsClass,
                           overrideInfo.embeddedPayload});
    setLooseWriteReport(report);
}

void setLooseAddFailed(const IFFile::SetLooseOverride &overrideInfo, const std::string &reason)
{
    if (!overrideInfo.active)
        return;

    if (overrideInfo.setId == 0 && overrideInfo.extensionForm.find("sky loose archive") != std::string::npos)
    {
        ypa_log_out("WARNING: OpenUA sky loose archive override failed for %s (%s); %s\n",
                    overrideInfo.requested.c_str(),
                    overrideInfo.resolvedPath.c_str(),
                    reason.c_str());
        return;
    }

    if (!setLooseEnsureReport(overrideInfo.setId))
        return;

    SetLooseReport &report = g_setLooseReports[overrideInfo.setId];
    std::string key = overrideInfo.requested + "\n" + overrideInfo.resolvedPath + "\n" +
                      (overrideInfo.emrs ? "emrs" : (overrideInfo.embedded ? "embedded" : "file")) + "\n" +
                      overrideInfo.sourceFunction;

    if (!report.failedKeys.insert(key).second)
        return;

    report.failed.push_back({overrideInfo.requested,
                             overrideInfo.resolvedPath,
                             overrideInfo.extensionForm,
                             reason,
                             overrideInfo.embedded,
                             overrideInfo.sourceFunction,
                             overrideInfo.emrs,
                             overrideInfo.emrsClass,
                             overrideInfo.embeddedPayload});
    setLooseWriteReport(report);
}

void setLooseAddLookup(int32_t setId,
                       const std::string &assetPath,
                       const std::string &originalPath,
                       bool originalExists,
                       const std::string &legacyPath,
                       bool legacyExists,
                       const char *sourceFunction,
                       bool embedded)
{
    if (!setLooseEnsureReport(setId))
        return;

    SetLooseReport &report = g_setLooseReports[setId];
    std::string source = sourceFunction ? sourceFunction : "IFFile::UAOpenFileWithSetLooseOverride";
    std::string key = assetPath + "\n" + originalPath + "\n" + legacyPath + "\n" + source;

    std::set<std::string> &keys = embedded ? report.embeddedLookupKeys : report.lookupKeys;
    std::vector<SetLooseLookupEntry> &lookups = embedded ? report.embeddedLookups : report.lookups;

    if (!keys.insert(key).second)
        return;

    lookups.push_back({assetPath,
                       originalPath,
                       originalExists,
                       legacyPath,
                       legacyExists,
                       source});
    setLooseWriteReport(report);
}

void setLooseAddEmrsLookup(int32_t setId,
                           const std::string &assetPath,
                           const std::string &className,
                           const std::string &payload,
                           size_t currentOffset,
                           const std::string &originalPath,
                           bool originalExists,
                           const std::string &legacyPath,
                           bool legacyExists,
                           const char *sourceFunction)
{
    if (!setLooseEnsureReport(setId))
        return;

    SetLooseReport &report = g_setLooseReports[setId];
    std::string source = sourceFunction ? sourceFunction : "NC_STACK_embed::LoadingFromIFF";
    std::string key = assetPath + "\n" + className + "\n" + payload + "\n" +
                      originalPath + "\n" + legacyPath + "\n" + source;

    if (!report.emrsLookupKeys.insert(key).second)
        return;

    std::string offsetLabel = currentOffset == (size_t)-1 ? "unknown" : setLooseFormatOffset(currentOffset);

    report.emrsLookups.push_back({assetPath,
                                  className,
                                  payload,
                                  offsetLabel,
                                  originalPath,
                                  originalExists,
                                  legacyPath,
                                  legacyExists,
                                  source});
    setLooseWriteReport(report);
}

std::string setLooseEmbeddedAssetFromRequest(const std::string &filename, int32_t setId)
{
    std::string assetPath = setLooseAssetFromRequest(filename, setId);
    if (!assetPath.empty())
        return assetPath;

    return setLooseStripLeadingSlashes(setLooseNormalizeSlashes(filename));
}

std::string setLooseSetBasSourceKind(const std::string &resolvedSource, FSMgr::iNode *node, int32_t setId)
{
    if (!node)
        return "unknown (resolved virtual path not found; raw stream owned below IFFile/FSMgr)";

    if (node->getType() != FSMgr::iNode::NTYPE_FILE)
        return "unknown (resolved virtual path is not a file)";

    std::string source = setLooseLower(setLooseNormalizeSlashes(resolvedSource));
    std::string objectsPrefix = "data/set" + std::to_string(setId) + "/objects/";

    if (source.find("/loose/") != std::string::npos)
        return "SET loose override path";

    if (source.compare(0, objectsPrefix.size(), objectsPrefix) == 0)
        return "normal loose Data/SetN/Objects SET.BAS style file";

    return "virtual FS path (filesystem file)";
}

void setLooseAddSetBasRawScan(const SetBasRawScanEntry &scan, int32_t setId)
{
    if (!setLooseEnsureReport(setId))
        return;

    SetLooseReport &report = g_setLooseReports[setId];
    std::string key = scan.openedAsset + "\n" + scan.resolvedSource + "\n" + scan.diskPath;

    if (!report.setBasRawScanKeys.insert(key).second)
        return;

    report.setBasRawScans.push_back(scan);
    setLooseWriteReport(report);
}

void setLooseAddSetBasParseTraceLine(int32_t setId, const std::string &line)
{
    if (!setLooseEnsureReport(setId))
        return;

    SetLooseReport &report = g_setLooseReports[setId];
    report.setBasParseTrace.push_back(line);
    setLooseWriteReport(report);
}

struct SkyLooseScopeState
{
    bool active = false;
    int depth = 0;
    std::string stem;
    std::vector<std::string> stemCandidates;
};

SkyLooseScopeState g_skyLooseScope;

std::string skyLooseUpper(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return (char)std::toupper(c);
    });
    return str;
}

void skyLoosePushUnique(std::vector<std::string> *items, const std::string &value)
{
    if (!items || value.empty())
        return;

    for (const std::string &existing : *items)
    {
        if (!StriCmp(existing, value))
            return;
    }
    items->push_back(value);
}

std::vector<std::string> skyLooseBuildStemCandidates(const std::string &stem)
{
    std::vector<std::string> result;
    skyLoosePushUnique(&result, stem);
    skyLoosePushUnique(&result, skyLooseUpper(stem));
    skyLoosePushUnique(&result, setLooseLower(stem));
    return result;
}

std::string skyLooseStemFromName(std::string name)
{
    name = setLooseTrimResourceName(setLooseStripLeadingSlashes(setLooseNormalizeSlashes(name)));
    if (name.empty())
        return std::string();

    size_t colonPos = name.find(':');
    if (colonPos != std::string::npos)
        name = setLooseStripLeadingSlashes(name.substr(colonPos + 1));

    std::string lower = setLooseLower(name);
    const std::string objectsTag = "objects/";
    size_t objectsPos = lower.rfind(objectsTag);
    if (objectsPos != std::string::npos)
        name = name.substr(objectsPos + objectsTag.size());

    std::string fileName = setLooseFileName(name);
    size_t dotPos = fileName.rfind('.');
    if (dotPos != std::string::npos)
        fileName.resize(dotPos);

    return setLooseTrimResourceName(fileName);
}

std::string skyLooseResourceName(std::string name)
{
    name = setLooseTrimResourceName(setLooseStripLeadingSlashes(setLooseNormalizeSlashes(name)));
    if (name.empty())
        return std::string();

    size_t colonPos = name.find(':');
    if (colonPos != std::string::npos)
        name = setLooseStripLeadingSlashes(name.substr(colonPos + 1));

    return setLooseTrimResourceName(name);
}

bool skyLooseClassSupported(const std::string &className)
{
    std::string cls = setLooseLower(className);
    return cls == "ilbm.class" ||
           cls == "sklt.class" ||
           cls == "bmpanim.class";
}

bool skyLooseResolveFirst(const std::vector<std::string> &candidates, std::string *openPath)
{
    for (const std::string &candidate : candidates)
    {
        std::string resolved;
        if (setLooseResolveReadableFile(candidate, &resolved))
        {
            if (openPath)
                *openPath = resolved;
            return true;
        }
    }
    return false;
}

void skyLoosePushPathCandidate(std::vector<std::string> *candidates, const std::string &path)
{
    if (!candidates || path.empty())
        return;

    skyLoosePushUnique(candidates, path);

    std::string legacyPath = setLooseNormalizeSlashes(correctSeparatorAndExt(path));
    if (legacyPath != setLooseNormalizeSlashes(path))
        skyLoosePushUnique(candidates, legacyPath);
}

std::vector<std::string> skyLooseClassFolders(const std::string &className)
{
    std::vector<std::string> folders;
    std::string cls = setLooseLower(className);

    if (cls == "sklt.class")
    {
        folders.push_back("SKLT");
        folders.push_back("Skeleton");
        folders.push_back("skeleton");
    }
    else if (cls == "ilbm.class")
    {
        folders.push_back("ILBM");
        folders.push_back("Textures");
        folders.push_back("Texture");
        folders.push_back("textures");
        folders.push_back("texture");
    }
    else if (cls == "bmpanim.class")
    {
        folders.push_back("VANM");
        folders.push_back("ANM");
        folders.push_back("Animations");
        folders.push_back("animations");
    }

    return folders;
}

std::vector<std::string> skyLooseBuildEmbeddedCandidates(const std::string &stem, const std::string &assetPath, const std::string &className, bool pngTexture)
{
    std::vector<std::string> candidates;
    std::string root = "Data/Objects/Loose/" + stem + "/";
    std::string normalizedAsset = setLooseStripLeadingSlashes(setLooseNormalizeSlashes(assetPath));
    std::string fileName = setLooseFileName(normalizedAsset);
    std::vector<std::string> classFolders = skyLooseClassFolders(className);

    if (normalizedAsset.empty())
        return candidates;

    if (pngTexture)
    {
        std::string exactUpper = setLooseReplaceExtension(normalizedAsset, ".PNG");
        std::string exactLower = setLooseReplaceExtension(normalizedAsset, ".png");
        std::string baseUpper = setLooseReplaceExtension(fileName, ".PNG");
        std::string baseLower = setLooseReplaceExtension(fileName, ".png");

        skyLoosePushPathCandidate(&candidates, root + exactUpper);
        skyLoosePushPathCandidate(&candidates, root + exactLower);
        skyLoosePushPathCandidate(&candidates, root + baseUpper);
        skyLoosePushPathCandidate(&candidates, root + baseLower);

        for (const std::string &folder : classFolders)
        {
            skyLoosePushPathCandidate(&candidates, root + folder + "/" + baseUpper);
            skyLoosePushPathCandidate(&candidates, root + folder + "/" + baseLower);
        }
    }
    else
    {
        skyLoosePushPathCandidate(&candidates, root + normalizedAsset);
        skyLoosePushPathCandidate(&candidates, root + fileName);

        for (const std::string &folder : classFolders)
            skyLoosePushPathCandidate(&candidates, root + folder + "/" + fileName);
    }

    return candidates;
}
}


bool IFFile::BeginSkyLooseScope(const std::string &skyName)
{
    std::string stem = skyLooseStemFromName(skyName);
    if (stem.empty())
        return false;

    g_skyLooseScope.active = true;
    g_skyLooseScope.depth++;
    g_skyLooseScope.stem = stem;
    g_skyLooseScope.stemCandidates = skyLooseBuildStemCandidates(stem);
    return true;
}

void IFFile::EndSkyLooseScope()
{
    if (g_skyLooseScope.depth > 0)
        g_skyLooseScope.depth--;

    if (g_skyLooseScope.depth <= 0)
    {
        g_skyLooseScope.active = false;
        g_skyLooseScope.depth = 0;
        g_skyLooseScope.stem.clear();
        g_skyLooseScope.stemCandidates.clear();
    }
}

bool IFFile::IsSkyLooseScopeActive()
{
    return g_skyLooseScope.active && !g_skyLooseScope.stem.empty();
}

bool IFFile::FindSkyLooseArchiveOverride(const std::string &filename, const std::string &mode, std::string *outPath, const char *sourceFunction)
{
    (void)sourceFunction;

    if (outPath)
        outPath->clear();

    if (!IsSkyLooseScopeActive() || !setLooseIsReadMode(mode))
        return false;

    std::string requestedStem = skyLooseStemFromName(filename);
    if (!requestedStem.empty() && StriCmp(requestedStem, g_skyLooseScope.stem))
        return false;

    std::vector<std::string> candidates;
    for (const std::string &stem : g_skyLooseScope.stemCandidates)
    {
        candidates.push_back("Data/Objects/Loose/" + stem + ".BASE");
        candidates.push_back("Data/Objects/Loose/" + stem + ".BAS");
        candidates.push_back("Data/Objects/Loose/" + stem + ".base");
        candidates.push_back("Data/Objects/Loose/" + stem + ".bas");
    }

    return skyLooseResolveFirst(candidates, outPath);
}

bool IFFile::FindSkyLooseEmrsOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction, size_t currentOffset)
{
    (void)currentOffset;

    if (out)
        *out = SetLooseOverride();

    if (!IsSkyLooseScopeActive() || !setLooseIsReadMode(mode) || !skyLooseClassSupported(className))
        return false;

    std::string assetPath = skyLooseResourceName(filename);
    if (assetPath.empty() || assetPath.find(':') != std::string::npos)
        return false;

    std::vector<std::string> candidates;
    for (const std::string &stem : g_skyLooseScope.stemCandidates)
    {
        std::vector<std::string> stemCandidates = skyLooseBuildEmbeddedCandidates(stem, assetPath, className, false);
        candidates.insert(candidates.end(), stemCandidates.begin(), stemCandidates.end());
    }

    std::string openPath;
    if (!skyLooseResolveFirst(candidates, &openPath))
        return false;

    if (out)
    {
        out->active = true;
        out->setId = 0;
        out->requested = assetPath;
        out->resolvedPath = openPath;
        out->extensionForm = "Data/Objects sky loose embedded override";
        out->embedded = true;
        out->sourceFunction = sourceFunction ? sourceFunction : "NC_STACK_embed::LoadingFromIFF";
        out->emrs = true;
        out->emrsClass = className;
        out->embeddedPayload = payload;
    }
    return true;
}

bool IFFile::FindSkyLooseEmrsPngOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction, size_t currentOffset)
{
    (void)currentOffset;

    if (out)
        *out = SetLooseOverride();

    if (!IsSkyLooseScopeActive() || !setLooseIsReadMode(mode))
        return false;

    if (setLooseLower(className) != "ilbm.class")
        return false;

    std::string assetPath = skyLooseResourceName(filename);
    if (assetPath.empty() || assetPath.find(':') != std::string::npos)
        return false;

    std::string ext = setLooseExtension(assetPath);
    if (ext != "ilbm" && ext != "ilb")
        return false;

    std::vector<std::string> candidates;
    for (const std::string &stem : g_skyLooseScope.stemCandidates)
    {
        std::vector<std::string> stemCandidates = skyLooseBuildEmbeddedCandidates(stem, assetPath, className, true);
        candidates.insert(candidates.end(), stemCandidates.begin(), stemCandidates.end());
    }

    std::string openPath;
    if (!skyLooseResolveFirst(candidates, &openPath))
        return false;

    if (out)
    {
        out->active = true;
        out->setId = 0;
        out->requested = assetPath;
        out->resolvedPath = openPath;
        out->extensionForm = "Data/Objects sky loose PNG texture override";
        out->embedded = true;
        out->sourceFunction = sourceFunction ? sourceFunction : "NC_STACK_embed::LoadingFromIFF";
        out->emrs = true;
        out->emrsClass = className;
        out->embeddedPayload = payload;
    }
    return true;
}


IFFile::IFFile(const std::string &diskPath, const std::string &mode)
: file_handle(diskPath, mode)
{
    ctxStack.emplace_front(TAG_FORM, TAG_NONE, 0x80000000, 0);
}


IFFile::IFFile(FSMgr::FileHandle *f, bool del)
: file_handle(f, false)
{
    ctxStack.emplace_front(TAG_FORM, TAG_NONE, 0x80000000, 0);
    
    if (f && del)
        delete f;
}

IFFile::IFFile(FSMgr::FileHandle &f)
: file_handle(&f, false)
{
    ctxStack.emplace_front(TAG_FORM, TAG_NONE, 0x80000000, 0);
}

IFFile::IFFile(FSMgr::FileHandle &&f)
: file_handle( std::move(f) )
{
    ctxStack.emplace_front(TAG_FORM, TAG_NONE, 0x80000000, 0);
}

IFFile *IFFile::RsrcOpenIFFile(const std::string &filename, const std::string &mode, const char *sourceFunction)
{
    IFFile result = UAOpenIFFile("rsrc:" + filename, mode, sourceFunction ? sourceFunction : "IFFile::RsrcOpenIFFile");
    if ( !result.OK() )
        return NULL;

    return new IFFile(std::move(result));
}

IFFile *IFFile::RsrcOpenIFFileVanilla(const std::string &filename, const std::string &mode)
{
    IFFile result = UAOpenIFFileVanilla("rsrc:" + filename, mode);
    if ( !result.OK() )
        return NULL;

    return new IFFile(std::move(result));
}

FSMgr::FileHandle IFFile::UAOpenFileVanilla(const std::string &filename, const std::string &mode)
{
    std::string tmpBuf = correctSeparatorAndExt( Common::Env.ApplyPrefix( filename ) );

    if ( !FSMgr::iDir::fileExist(tmpBuf) )
        return FSMgr::FileHandle();

    return FSMgr::iDir::openFile(tmpBuf, mode);
}

IFFile IFFile::UAOpenIFFileVanilla(const std::string &filename, const std::string &mode)
{
    FSMgr::FileHandle fil = UAOpenFileVanilla(filename, mode);
    if ( !fil.OK() )
        return IFFile();

    return IFFile(std::move(fil));
}

bool IFFile::FindSetLooseOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction)
{
    if (out)
        *out = SetLooseOverride();

    if ( !setLooseIsReadMode(mode) )
        return false;

    int32_t setId = setLooseCurrentSetId();
    if ( !setId )
        return false;

    std::string assetPath = setLooseAssetFromRequest(filename, setId);
    if ( assetPath.empty() || !setLooseSupportedExtension(assetPath) )
        return false;

    assetPath = setLooseStripLeadingSlashes(setLooseNormalizeSlashes(assetPath));
    if ( assetPath.find(':') != std::string::npos )
        return false;

    if ( !setLooseEnsureReport(setId) )
        return false;

    std::string looseRoot = "Data/Set" + std::to_string(setId) + "/Loose/";
    std::string originalPath = looseRoot + assetPath;
    std::string legacyPath = setLooseNormalizeSlashes(correctSeparatorAndExt(originalPath));
    bool originalExists = FSMgr::iDir::fileExist(originalPath);
    bool legacyExists = FSMgr::iDir::fileExist(legacyPath);

    setLooseAddLookup(setId,
                      assetPath,
                      originalPath,
                      originalExists,
                      legacyPath,
                      legacyExists,
                      sourceFunction,
                      false);

    struct Candidate
    {
        std::string path;
        std::string extensionForm;
        bool exists = false;
    };

    std::vector<Candidate> candidates;
    candidates.push_back({originalPath, "original requested extension form", originalExists});

    if ( legacyPath != setLooseNormalizeSlashes(originalPath) )
        candidates.push_back({legacyPath, "legacy 3-letter extension form", legacyExists});

    for (const Candidate &candidate : candidates)
    {
        if ( candidate.exists )
        {
            if (out)
            {
                out->active = true;
                out->setId = setId;
                out->requested = assetPath;
                out->resolvedPath = candidate.path;
                out->extensionForm = candidate.extensionForm;
                out->vanillaPath = setLooseNormalizeSlashes(correctSeparatorAndExt(Common::Env.ApplyPrefix(filename)));
                out->embedded = false;
                out->sourceFunction = sourceFunction ? sourceFunction : "IFFile::UAOpenFileWithSetLooseOverride";
            }
            return true;
        }
    }

    return false;
}

bool IFFile::FindSetLooseEmbeddedOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction)
{
    if (out)
        *out = SetLooseOverride();

    if ( !setLooseIsReadMode(mode) )
        return false;

    int32_t setId = setLooseCurrentSetId();
    if ( !setId )
        return false;

    std::string assetPath = setLooseEmbeddedAssetFromRequest(filename, setId);
    if ( assetPath.empty() || !setLooseSupportedExtension(assetPath) )
        return false;

    assetPath = setLooseStripLeadingSlashes(setLooseNormalizeSlashes(assetPath));
    if ( assetPath.find(':') != std::string::npos )
        return false;

    if ( !setLooseEnsureReport(setId) )
        return false;

    std::string looseRoot = "Data/Set" + std::to_string(setId) + "/Loose/";
    std::string originalPath = looseRoot + assetPath;
    std::string legacyPath = setLooseNormalizeSlashes(correctSeparatorAndExt(originalPath));
    bool originalExists = FSMgr::iDir::fileExist(originalPath);
    bool legacyExists = FSMgr::iDir::fileExist(legacyPath);

    setLooseAddLookup(setId,
                      assetPath,
                      originalPath,
                      originalExists,
                      legacyPath,
                      legacyExists,
                      sourceFunction,
                      true);

    struct Candidate
    {
        std::string path;
        std::string extensionForm;
        bool exists = false;
    };

    std::vector<Candidate> candidates;
    candidates.push_back({originalPath, "original requested extension form", originalExists});

    if ( legacyPath != setLooseNormalizeSlashes(originalPath) )
        candidates.push_back({legacyPath, "legacy 3-letter extension form", legacyExists});

    for (const Candidate &candidate : candidates)
    {
        if ( candidate.exists )
        {
            if (out)
            {
                out->active = true;
                out->setId = setId;
                out->requested = assetPath;
                out->resolvedPath = candidate.path;
                out->extensionForm = candidate.extensionForm;
                out->embedded = true;
                out->sourceFunction = sourceFunction ? sourceFunction : "IFFile::UAOpenFileWithSetLooseEmbeddedOverride";
            }
            return true;
        }
    }

    return false;
}

bool IFFile::FindSetLooseEmrsOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction, size_t currentOffset)
{
    (void)currentOffset;

    if (out)
        *out = SetLooseOverride();

    if ( !setLooseIsReadMode(mode) )
        return false;

    int32_t setId = setLooseCurrentSetId();
    if ( !setId )
        return false;

    std::string assetPath = setLooseEmbeddedAssetFromRequest(filename, setId);
    if ( assetPath.empty() )
        return false;

    assetPath = setLooseStripLeadingSlashes(setLooseNormalizeSlashes(assetPath));
    if ( assetPath.find(':') != std::string::npos )
        return false;

    if ( !setLooseEnsureReport(setId) )
        return false;

    std::string looseRoot = "Data/Set" + std::to_string(setId) + "/Loose/";
    std::string originalPath = looseRoot + assetPath;
    std::string legacyPath = setLooseNormalizeSlashes(correctSeparatorAndExt(originalPath));
    bool originalExists = FSMgr::iDir::fileExist(originalPath);
    bool legacyExists = FSMgr::iDir::fileExist(legacyPath);

    g_setLooseReports[setId].emrsResourcesChecked++;

    struct Candidate
    {
        std::string path;
        std::string extensionForm;
        bool exists = false;
    };

    std::vector<Candidate> candidates;
    candidates.push_back({originalPath, "original requested extension form", originalExists});

    if ( legacyPath != setLooseNormalizeSlashes(originalPath) )
        candidates.push_back({legacyPath, "legacy 3-letter extension form", legacyExists});

    for (const Candidate &candidate : candidates)
    {
        if ( candidate.exists )
        {
            if (out)
            {
                out->active = true;
                out->setId = setId;
                out->requested = assetPath;
                out->resolvedPath = candidate.path;
                out->extensionForm = candidate.extensionForm;
                out->embedded = true;
                out->sourceFunction = sourceFunction ? sourceFunction : "NC_STACK_embed::LoadingFromIFF";
                out->emrs = true;
                out->emrsClass = className;
                out->embeddedPayload = payload;
            }
            return true;
        }
    }

    return false;
}

bool IFFile::FindSetLooseEmrsPngOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction, size_t currentOffset)
{
    (void)currentOffset;

    if (out)
        *out = SetLooseOverride();

    if ( !setLooseIsReadMode(mode) )
        return false;

    if ( setLooseLower(className) != "ilbm.class" )
        return false;

    int32_t setId = setLooseCurrentSetId();
    if ( !setId )
        return false;

    std::string assetPath = setLooseEmbeddedAssetFromRequest(filename, setId);
    if ( assetPath.empty() )
        return false;

    assetPath = setLooseTrimResourceName(setLooseStripLeadingSlashes(setLooseNormalizeSlashes(assetPath)));
    if ( assetPath.find(':') != std::string::npos )
        return false;

    std::string assetBase = setLooseLower(setLooseFileName(assetPath));
    if ( assetBase == "fx1.ilbm" || assetBase == "fx1.ilb" ||
         assetBase == "fx2.ilbm" || assetBase == "fx2.ilb" ||
         assetBase == "fx3.ilbm" || assetBase == "fx3.ilb" )
        return false;

    std::string ext = setLooseExtension(assetPath);
    if ( ext != "ilbm" && ext != "ilb" )
        return false;

    if ( !setLooseEnsureReport(setId) )
        return false;

    std::string looseRoot = "Data/Set" + std::to_string(setId) + "/Loose/";
    std::vector<std::string> candidates;
    candidates.push_back(looseRoot + setLooseReplaceExtension(assetPath, ".PNG"));
    std::string lowerCandidate = looseRoot + setLooseReplaceExtension(assetPath, ".png");
    if ( setLooseNormalizeSlashes(lowerCandidate) != setLooseNormalizeSlashes(candidates.front()) )
        candidates.push_back(lowerCandidate);

    for (const std::string &candidate : candidates)
    {
        std::string openPath;
        if ( setLooseResolveReadableFile(candidate, &openPath) )
        {
            if (out)
            {
                out->active = true;
                out->setId = setId;
                out->requested = assetPath;
                out->resolvedPath = openPath;
                out->extensionForm = "PNG texture override";
                out->embedded = true;
                out->sourceFunction = sourceFunction ? sourceFunction : "NC_STACK_embed::LoadingFromIFF";
                out->emrs = true;
                out->emrsClass = className;
                out->embeddedPayload = payload;
            }
            return true;
        }
    }

    return false;
}

bool IFFile::FindSetHiEffectPngOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction)
{
    if (out)
        *out = SetLooseOverride();

    if ( !setLooseIsReadMode(mode) )
        return false;

    int32_t setId = setLooseCurrentSetId();
    if ( !setId )
        return false;

    std::string assetPath = setLooseTrimResourceName(setLooseStripLeadingSlashes(setLooseNormalizeSlashes(filename)));
    if ( assetPath.empty() || assetPath.find(':') != std::string::npos )
        return false;

    std::string assetLower = setLooseLower(assetPath);
    if ( assetLower.compare(0, 3, "hi/") != 0 )
        return false;

    std::string assetBase = setLooseFileName(assetPath);
    std::string assetBaseLower = setLooseLower(assetBase);
    if ( assetBaseLower != "fx1.ilbm" && assetBaseLower != "fx1.ilb" &&
         assetBaseLower != "fx2.ilbm" && assetBaseLower != "fx2.ilb" &&
         assetBaseLower != "fx3.ilbm" && assetBaseLower != "fx3.ilb" )
        return false;

    if ( !setLooseEnsureReport(setId) )
        return false;

    std::string setRoot = "Data/Set" + std::to_string(setId) + "/";
    std::vector<std::string> candidates;
    candidates.push_back(setRoot + setLooseReplaceExtension(assetPath, ".PNG"));

    std::string lowerCandidate = setRoot + setLooseReplaceExtension(assetPath, ".png");
    if ( setLooseNormalizeSlashes(lowerCandidate) != setLooseNormalizeSlashes(candidates.front()) )
        candidates.push_back(lowerCandidate);

    for (const std::string &candidate : candidates)
    {
        std::string openPath;
        if ( setLooseResolveReadableFile(candidate, &openPath) )
        {
            if (out)
            {
                out->active = true;
                out->setId = setId;
                out->requested = assetPath;
                out->resolvedPath = openPath;
                out->extensionForm = "HI PNG effect override";
                out->vanillaPath = setLooseNormalizeSlashes(correctSeparatorAndExt(Common::Env.ApplyPrefix("rsrc:" + assetPath)));
                out->embedded = false;
                out->sourceFunction = sourceFunction ? sourceFunction : "NC_STACK_ilbm::rsrc_func64";
            }
            return true;
        }
    }

    return false;
}

FSMgr::FileHandle IFFile::UAOpenFileWithSetLooseOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction)
{
    SetLooseOverride overrideInfo;

    std::string skyArchiveOverridePath;
    if ( FindSkyLooseArchiveOverride(filename, mode, &skyArchiveOverridePath, sourceFunction) )
    {
        FSMgr::FileHandle fil = FSMgr::iDir::openFile(skyArchiveOverridePath, mode);
        if ( fil.OK() )
        {
            overrideInfo.active = true;
            overrideInfo.setId = 0;
            overrideInfo.requested = filename;
            overrideInfo.resolvedPath = skyArchiveOverridePath;
            overrideInfo.extensionForm = "Data/Objects sky loose archive override";
            overrideInfo.vanillaPath = setLooseNormalizeSlashes(correctSeparatorAndExt(Common::Env.ApplyPrefix(filename)));
            overrideInfo.sourceFunction = sourceFunction ? sourceFunction : "IFFile::UAOpenFileWithSetLooseOverride";

            if (out)
                *out = overrideInfo;

            return fil;
        }

        ypa_log_out("WARNING: OpenUA sky loose archive override failed to open: %s; vanilla Data/Objects fallback used.\n", skyArchiveOverridePath.c_str());
    }

    if ( FindSetLooseOverride(filename, mode, &overrideInfo, sourceFunction) )
    {
        FSMgr::FileHandle fil = FSMgr::iDir::openFile(overrideInfo.resolvedPath, mode);
        if ( fil.OK() )
        {
            if (out)
                *out = overrideInfo;

            return fil;
        }

        setLooseAddFailed(overrideInfo, "loose file existed but failed to load; vanilla fallback used.");
    }

    if (out)
        *out = SetLooseOverride();

    return UAOpenFileVanilla(filename, mode);
}

FSMgr::FileHandle IFFile::UAOpenFileWithSetLooseEmbeddedOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction)
{
    SetLooseOverride overrideInfo;

    if ( FindSetLooseEmbeddedOverride(filename, mode, &overrideInfo, sourceFunction) )
    {
        FSMgr::FileHandle fil = FSMgr::iDir::openFile(overrideInfo.resolvedPath, mode);
        if ( fil.OK() )
        {
            if (out)
                *out = overrideInfo;

            return fil;
        }

        setLooseAddFailed(overrideInfo, "loose embedded override existed but failed to open; embedded SET.BAS fallback used.");
    }

    if (out)
        *out = SetLooseOverride();

    return FSMgr::FileHandle();
}

FSMgr::FileHandle IFFile::UAOpenFileWithSetLooseEmrsOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction, size_t currentOffset)
{
    SetLooseOverride overrideInfo;

    if ( FindSetLooseEmrsOverride(filename, mode, className, payload, &overrideInfo, sourceFunction, currentOffset) )
    {
        FSMgr::FileHandle fil = FSMgr::iDir::openFile(overrideInfo.resolvedPath, mode);
        if ( fil.OK() )
        {
            if (out)
                *out = overrideInfo;

            return fil;
        }

        setLooseAddFailed(overrideInfo, "loose EMRS override existed but failed to open; embedded payload fallback used.");
    }

    if (out)
        *out = SetLooseOverride();

    return FSMgr::FileHandle();
}

IFFile IFFile::UAOpenIFFile(const std::string &filename, const std::string &mode, const char *sourceFunction)
{
    SetLooseOverride overrideInfo;
    FSMgr::FileHandle fil = UAOpenFileWithSetLooseOverride(filename, mode, &overrideInfo, sourceFunction ? sourceFunction : "IFFile::UAOpenIFFile");

    if ( !fil.OK() )
        return IFFile();

    IFFile result(std::move(fil));
    result._setLooseOverride = overrideInfo;
    return result;
}

IFFile IFFile::UAOpenIFFileWithSetLooseEmbeddedOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction)
{
    SetLooseOverride overrideInfo;
    FSMgr::FileHandle fil = UAOpenFileWithSetLooseEmbeddedOverride(filename, mode, &overrideInfo, sourceFunction);

    if ( !fil.OK() )
        return IFFile();

    IFFile result(std::move(fil));
    result._setLooseOverride = overrideInfo;

    if (out)
        *out = overrideInfo;

    return result;
}

IFFile IFFile::UAOpenIFFileWithSetLooseEmrsOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction, size_t currentOffset)
{
    SetLooseOverride overrideInfo;
    FSMgr::FileHandle fil = UAOpenFileWithSetLooseEmrsOverride(filename, mode, className, payload, &overrideInfo, sourceFunction, currentOffset);

    if ( !fil.OK() )
        return IFFile();

    IFFile result(std::move(fil));
    result._setLooseOverride = overrideInfo;

    if (out)
        *out = overrideInfo;

    return result;
}

bool IFFile::IsSetLooseOverride() const
{
    return _setLooseOverride.active;
}

void IFFile::ReportSetLooseOverrideUsed() const
{
    setLooseAddUsed(_setLooseOverride);
}

void IFFile::ReportSetLooseOverrideFailed(const std::string &reason) const
{
    setLooseAddFailed(_setLooseOverride, reason);
}

void IFFile::ReportSetLooseOverrideUsed(const SetLooseOverride &overrideInfo)
{
    setLooseAddUsed(overrideInfo);
}

void IFFile::ReportSetLooseOverrideFailed(const SetLooseOverride &overrideInfo, const std::string &reason)
{
    setLooseAddFailed(overrideInfo, reason);
}

void IFFile::ReportSetBasRawScan(const std::string &filename, const char *sourceFunction)
{
    int32_t setId = setLooseCurrentSetId();
    if ( !setId || !setLooseEnsureReport(setId) )
        return;

    SetLooseOverride overrideInfo;
    std::string resolvedSource;
    std::string diskPath;
    std::string rawStatus = "ok";

    if ( FindSetLooseOverride(filename, "rb", &overrideInfo, sourceFunction ? sourceFunction : "NC_STACK_base::LoadBaseFromFile") )
    {
        resolvedSource = overrideInfo.resolvedPath;
    }
    else
    {
        resolvedSource = setLooseNormalizeSlashes(correctSeparatorAndExt(Common::Env.ApplyPrefix(filename)));
    }

    FSMgr::iNode *node = FSMgr::iDir::findNode(resolvedSource);
    if ( node )
        diskPath = node->getPath();

    SetBasRawScanEntry scan;
    scan.openedAsset = setLooseEmbeddedAssetFromRequest(filename, setId);
    scan.resolvedSource = resolvedSource;
    scan.diskPath = diskPath.empty() ? "<unknown>" : diskPath;
    scan.sourceKind = setLooseSetBasSourceKind(resolvedSource, node, setId);
    scan.sourceFunction = sourceFunction ? sourceFunction : "NC_STACK_base::LoadBaseFromFile";

    const std::vector<std::string> markerNames = {
        "EMRS",
        "ilbm.class",
        "MTL.ILBM",
        "BODEN1.ILBM",
        "BODEN2.ILBM",
        "BODEN5.ILBM",
        "CITY1.ILBM",
        "FORM",
        "VBMP",
        "HEAD",
        "BODY"
    };

    if ( !node || node->getType() != FSMgr::iNode::NTYPE_FILE )
    {
        scan.rawStatus = "raw bytes unavailable at IFFile layer; lower owner is FSMgr::iDir::openFile";
        for (const std::string &marker : markerNames)
            scan.markers.push_back({marker, std::vector<size_t>()});
        setLooseAddSetBasRawScan(scan, setId);
        return;
    }

    FSMgr::FileHandle fil = FSMgr::iDir::openFile(node, "rb");
    if ( !fil.OK() )
    {
        scan.rawStatus = "raw bytes unavailable: FSMgr::iDir::openFile failed";
        for (const std::string &marker : markerNames)
            scan.markers.push_back({marker, std::vector<size_t>()});
        setLooseAddSetBasRawScan(scan, setId);
        return;
    }

    fil.seek(0, SEEK_END);
    scan.fileSize = fil.tell();
    fil.seek(0, SEEK_SET);

    std::vector<uint8_t> data(scan.fileSize);
    if ( scan.fileSize && fil.read(data.data(), scan.fileSize) != scan.fileSize )
        rawStatus = "raw read was partial";

    scan.rawStatus = rawStatus;
    scan.firstBytesHex = setLooseFirstBytesHex(data);

    if (data.size() >= 12 && setLooseReadU32BE(data, 0) == TAG_FORM)
        scan.firstFormType = setLooseTagToString(setLooseReadU32BE(data, 8));
    else
        scan.firstFormType = "<not FORM>";

    scan.topLevelChunks = setLooseTopLevelChunks(data);

    for (const std::string &marker : markerNames)
        scan.markers.push_back({marker, setLooseFindMarkerOffsets(data, marker)});

    setLooseAddSetBasRawScan(scan, setId);
}

void IFFile::FlushSetLooseOverrideReport()
{
    int32_t setId = setLooseCurrentSetId();
    if ( !setId || !setLooseEnsureReport(setId) )
        return;

    setLooseWriteReport(g_setLooseReports[setId]);
}

void IFFile::BeginSetBasParseTrace(const std::string &filename)
{
    int32_t setId = setLooseCurrentSetId();
    if ( !setId || !setLooseEnsureReport(setId) )
        return;

    g_setBasParseTraceActive = true;
    g_setBasParseTraceSetId = setId;
    g_setBasParseTraceCount = 0;

    std::string asset = setLooseEmbeddedAssetFromRequest(filename, setId);
    SetLooseReport &report = g_setLooseReports[setId];
    if (report.setBasParseTrace.empty())
        setLooseAddSetBasParseTraceLine(setId, "opened asset: " + asset);
}

void IFFile::EndSetBasParseTrace()
{
    if ( g_setBasParseTraceActive && g_setBasParseTraceSetId )
        setLooseAddSetBasParseTraceLine(g_setBasParseTraceSetId, "trace ended");

    g_setBasParseTraceActive = false;
    g_setBasParseTraceSetId = 0;
    g_setBasParseTraceCount = 0;
}

bool IFFile::IsSetBasParseTraceActive()
{
    return g_setBasParseTraceActive && g_setBasParseTraceSetId;
}

void IFFile::TraceSetBasParse(const char *section, const std::string &message, const IFFile *mfile)
{
    if ( !IsSetBasParseTraceActive() )
        return;

    if ( g_setBasParseTraceCount >= SETBAS_PARSE_TRACE_LIMIT )
        return;

    std::string line;
    if (section && section[0])
    {
        line += section;
        line += ": ";
    }

    if (mfile)
    {
        line += "[tell ";
        line += setLooseFormatOffset(mfile->tell());
        line += "] ";
    }

    line += message;
    g_setBasParseTraceCount++;

    if ( g_setBasParseTraceCount == SETBAS_PARSE_TRACE_LIMIT )
        line += " (trace limit reached)";

    setLooseAddSetBasParseTraceLine(g_setBasParseTraceSetId, line);
}

std::string IFFile::ChunkLabel(const Context &chunk)
{
    return setLooseChunkLabel(chunk);
}

int IFFile::pushChunk(uint32_t TAG1, uint32_t TAG2, int32_t TAG_SZ)
{
    int32_t position = 0;

    if ( file_handle.IsWriting() )
    {
        Context &currentCtx = ctxStack.front();

        if ( currentCtx.TAG != TAG_FORM )
            return IFF_ERR_SYNTAX;

        if ( !file_handle.writeU32B(TAG2) )
            return IFF_ERR_WRITE;

        if ( !file_handle.writeU32B(TAG_SZ) )
            return IFF_ERR_WRITE;

        if ( TAG2 == TAG_FORM )
        {
            if ( !file_handle.writeU32B(TAG1) )
                return IFF_ERR_WRITE;

            position = 4;
        }
        else
        {
            TAG1 = currentCtx.TAG_EXTENSION;
        }
    }
    else                                          // READING
    {
        Context &currentCtx = ctxStack.front();

        if ( currentCtx.TAG != TAG_FORM )
            return IFF_ERR_EOC;

        if ( currentCtx.TAG_SIZE == currentCtx.position )
            return IFF_ERR_EOC;

        TAG2 = file_handle.readU32B();

        TAG_SZ = file_handle.readU32B();

        TAG1 = currentCtx.TAG_EXTENSION;

        if ( TAG2 == TAG_FORM )
        {
            TAG1 = file_handle.readU32B();

            position = 4;
        }
    }

    ctxStack.emplace_front(TAG2, TAG1, TAG_SZ, position);

    depth++;

    return IFF_ERR_OK;
}

int IFFile::popChunk()
{
    Context &ctx = ctxStack.front();
    int TAG_SZ = ctx.TAG_SIZE;

    if ( ctx.TAG == TAG_FORM && ctx.TAG_EXTENSION == TAG_NONE )
        return IFF_ERR_SYNTAX;

    if ( file_handle.IsWriting() )
    {
        if ( TAG_SZ == -1 ) // UNKNOWN SIZE
        {
            TAG_SZ = ctx.position;

            if ( file_handle.seek(-(ctx.position + 4), SEEK_CUR) ) // seek for write TAG SIZE
                return IFF_ERR_SEEK;

            if ( !file_handle.writeU32B(ctx.position) )
                return IFF_ERR_WRITE;

            if ( file_handle.seek(ctx.position, SEEK_CUR) )
                return IFF_ERR_SEEK;
        }

        if ( ctx.position < TAG_SZ ) // if we not in the end
            return IFF_ERR_CORRUPT;

        if ( TAG_SZ & 1 ) // pad 1 byte
        {
            if ( !file_handle.writeU8(0) )
                return -IFF_ERR_WRITE;

            TAG_SZ++;
        }

        ctxStack.pop_front();

        ctxStack.front().position += TAG_SZ + 8;

        depth--;
    }
    else
    {
        if ( ctx.TAG != TAG_FORM )
        {
            if ( ctx.position < TAG_SZ && file_handle.seek(TAG_SZ -  ctx.position, SEEK_CUR) )
                return IFF_ERR_SEEK;

            if ( TAG_SZ & 1 ) // pad 1 byte
            {
                if ( file_handle.seek(1, SEEK_CUR) )
                    return IFF_ERR_SEEK;

                TAG_SZ++;
            }
        }

        ctxStack.pop_front();

        Context &ctx2 = ctxStack.front();
        if ( ctx2.TAG == TAG_FORM && ctx2.TAG_EXTENSION == TAG_NONE )
            return IFF_ERR_EOF;

        ctx2.position += TAG_SZ + 8;
        depth--;
    }

    return IFF_ERR_OK;
}

int IFFile::parse()
{
    int res = IFF_ERR_SYNTAX;

    if (_flagPop)
    {
        res = popChunk();
        _flagPop = false;

        if (res != IFF_ERR_OK)
            return res;
    }

    res = pushChunk(TAG_NONE, TAG_NONE, -1);
    if ( res == IFF_ERR_EOC )
        _flagPop = true;

    return res;
}

bool IFFile::skipChunk()
{
    while ( 1 )
    {
        int res = parse();

        if (res == IFF_ERR_EOC)
            break;
        else if ( res )
            return false;

        if ( !skipChunk() )
            return false;
    }

    return true;
}

size_t IFFile::read(void *buf, size_t sz)
{
    Context &ctx = ctxStack.front();

    if ( ctx.Is(TAG_FORM, TAG_NONE) )
        return IFF_ERR_SYNTAX;

    if ( ctx.TAG == TAG_FORM )
        return IFF_ERR_SYNTAX;

    if ( sz + ctx.position > (size_t)ctx.TAG_SIZE )
    {
        if (ctx.TAG_SIZE == ctx.position)
            return 0;

        sz = ctx.TAG_SIZE - ctx.position;
    }

    ctx.position += sz;

    size_t readed = file_handle.read(buf, sz);

    if ( readed != sz )
        return IFF_ERR_READ;

    return readed;
}

size_t IFFile::write(const void *buf, size_t sz)
{
    Context &ctx = ctxStack.front();

    if ( ctx.Is(TAG_FORM, TAG_NONE) )
        return IFF_ERR_SYNTAX;

    if ( ctx.TAG == TAG_FORM )
        return IFF_ERR_SYNTAX;

    if ( ctx.TAG_SIZE != -1 &&
            ctx.position + sz > (size_t)ctx.TAG_SIZE)
    {
        if ( ctx.TAG_SIZE == ctx.position )
            return 0;

        sz = ctx.TAG_SIZE - ctx.position;
    }

    ctx.position += sz;
    size_t writed = file_handle.write(buf, sz);

    if ( writed != sz )
        return IFF_ERR_WRITE;

    return writed;
}

const IFFile::Context &IFFile::GetCurrentChunk()
{
    return ctxStack.front();
}

std::string IFFile::readStr(int maxSz)
{
    char *bf = new char[maxSz]();
    std::string tmp(bf, read(bf, maxSz));
    delete[] bf;
    return tmp;
}

std::string IFFile::PeekNextChunkLabel()
{
    if ( !file_handle.OK() )
        return "unknown";

    size_t pos = file_handle.tell();
    uint32_t tag = file_handle.readU32B();
    file_handle.readU32B();

    if ( file_handle.readErr() )
    {
        file_handle.seek((long int)pos, SEEK_SET);
        return "unknown";
    }

    std::string label = setLooseTagToString(tag);
    if ( tag == TAG_FORM )
    {
        uint32_t ext = file_handle.readU32B();
        if ( !file_handle.readErr() )
            label += " " + setLooseTagToString(ext);
    }

    file_handle.seek((long int)pos, SEEK_SET);
    return label;
}


bool IFFile::eof() const
{
    return file_handle.eof();
}

bool IFFile::OK() const
{
    return file_handle.OK();
}

size_t IFFile::tell() const
{
    return file_handle.tell();
}

int IFFile::seek(long int offset, int origin)
{
    return 0;
}

void IFFile::close()
{
    file_handle.close();
}
