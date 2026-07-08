#ifndef IFFile_H_INCLUDED
#define IFFile_H_INCLUDED

#include <deque>
#include "system/fsmgr.h"

class IFFile : public FSMgr::iFileHandle
{
public:
    struct Context
    {
        uint32_t TAG = 0;
        uint32_t TAG_EXTENSION = 0;
        int32_t TAG_SIZE = 0;
        int32_t position = 0;
        
        Context() = default;
        Context(uint32_t tag, uint32_t tag_ext, int32_t tag_sz, int32_t pos) 
        : TAG(tag), TAG_EXTENSION(tag_ext), TAG_SIZE(tag_sz), position(pos) 
        {}
        
        bool Is(uint32_t tag) const
        {
            return TAG == tag;
        }
        
        bool Is(uint32_t tag, uint32_t ext) const
        {
            return TAG == tag && TAG_EXTENSION == ext;
        }

    };

    enum IFF_ERR
    {
        IFF_ERR_OK    = 0, // OK
        IFF_ERR_EOF   = -1, // End of file
        IFF_ERR_EOC   = -2, // End of context(TAG)
        IFF_ERR_MEM   = -4, // Memory allocation failed
        IFF_ERR_READ  = -5, // Read error
        IFF_ERR_WRITE = -6, // Write error
        IFF_ERR_SEEK  = -7, // Seek error
        IFF_ERR_CORRUPT = -8, // Data corrupted
        IFF_ERR_SYNTAX  = -9
    };

    enum IFF_FLAGS
    {
        IFF_FLAGS_POP     = 2
    };
    
    IFFile() = default;

    IFFile(const std::string &diskPath, const std::string &mode);
    virtual ~IFFile() = default;
    
    IFFile(IFFile &&) = default;
    IFFile& operator=(IFFile &&) = default;
    
    IFFile(FSMgr::FileHandle *, bool del = true);
    IFFile(FSMgr::FileHandle &);
    IFFile(FSMgr::FileHandle &&);
    
    IFFile(const IFFile&) = delete;
    IFFile& operator=(const IFFile &) = delete;
    
    
    
    virtual void close() override;
    

    int pushChunk(uint32_t TAG1, uint32_t TAG2, int32_t TAG_SZ);
    int popChunk();
    int parse();
    bool skipChunk();
    
    size_t read(void *buf, size_t sz) override;
    size_t write(const void *buf, size_t sz) override;
    const IFFile::Context &GetCurrentChunk(); 
    
    virtual bool eof() const override;
    virtual bool OK() const override;
    virtual size_t tell() const override;
    virtual int seek(long int offset, int origin) override;

    std::string readStr(int maxSz);
    std::string PeekNextChunkLabel();

    struct SetLooseOverride
    {
        bool active = false;
        int32_t setId = 0;
        std::string requested;
        std::string resolvedPath;
        std::string extensionForm;
        std::string vanillaPath;
        bool embedded = false;
        std::string sourceFunction;
        bool emrs = false;
        std::string emrsClass;
        std::string embeddedPayload;
    };

    bool IsSetLooseOverride() const;
    void ReportSetLooseOverrideUsed() const;
    void ReportSetLooseOverrideFailed(const std::string &reason) const;
    static void ReportSetLooseOverrideUsed(const SetLooseOverride &overrideInfo);
    static void ReportSetLooseOverrideFailed(const SetLooseOverride &overrideInfo, const std::string &reason);
    static void ReportSetBasRawScan(const std::string &filename, const char *sourceFunction = NULL);
    static void FlushSetLooseOverrideReport();
    static void BeginSetBasParseTrace(const std::string &filename);
    static void EndSetBasParseTrace();
    static bool IsSetBasParseTraceActive();
    static void TraceSetBasParse(const char *section, const std::string &message, const IFFile *mfile = NULL);
    static std::string ChunkLabel(const Context &chunk);

    static bool FindSetLooseOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction = NULL);
    static bool FindSetLooseEmbeddedOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction = NULL);
    static bool FindSetLooseEmrsOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction = NULL, size_t currentOffset = (size_t)-1);
    static bool FindSetLooseEmrsPngOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction = NULL, size_t currentOffset = (size_t)-1);
    static bool FindSetHiEffectPngOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction = NULL);

    static bool BeginSkyLooseScope(const std::string &skyName);
    static void EndSkyLooseScope();
    static bool IsSkyLooseScopeActive();
    static bool FindSkyLooseArchiveOverride(const std::string &filename, const std::string &mode, std::string *outPath, const char *sourceFunction = NULL);
    static bool FindSkyLooseEmrsOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction = NULL, size_t currentOffset = (size_t)-1);
    static bool FindSkyLooseEmrsPngOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction = NULL, size_t currentOffset = (size_t)-1);

    static FSMgr::FileHandle UAOpenFileWithSetLooseOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction = NULL);
    static FSMgr::FileHandle UAOpenFileWithSetLooseEmbeddedOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction = NULL);
    static FSMgr::FileHandle UAOpenFileWithSetLooseEmrsOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction = NULL, size_t currentOffset = (size_t)-1);
    static FSMgr::FileHandle UAOpenFileVanilla(const std::string &filename, const std::string &mode);
    static IFFile *RsrcOpenIFFileVanilla(const std::string &filename, const std::string &mode);
    static IFFile UAOpenIFFileVanilla(const std::string &filename, const std::string &mode);
    static IFFile UAOpenIFFileWithSetLooseEmbeddedOverride(const std::string &filename, const std::string &mode, SetLooseOverride *out, const char *sourceFunction = NULL);
    static IFFile UAOpenIFFileWithSetLooseEmrsOverride(const std::string &filename, const std::string &mode, const std::string &className, const std::string &payload, SetLooseOverride *out, const char *sourceFunction = NULL, size_t currentOffset = (size_t)-1);
    static IFFile *RsrcOpenIFFile(const std::string &filename, const std::string &mode, const char *sourceFunction = NULL);
    static IFFile UAOpenIFFile(const std::string &filename, const std::string &mode, const char *sourceFunction = NULL);

protected:
    FSMgr::FileHandle file_handle;
    SetLooseOverride _setLooseOverride;
    bool _flagPop = false;
    int depth = 0;
    std::deque<Context> ctxStack;
};

#endif // MFILE_H_INCLUDED
