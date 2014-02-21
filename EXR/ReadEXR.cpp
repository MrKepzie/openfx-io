/*
 OFX exrReader plugin.
 Reads a an input image using the OpenEXR library.
 
 Copyright (C) 2013 INRIA
 Author Alexandre Gauthier-Foichat alexandre.gauthier-foichat@inria.fr
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 
 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 
 */
#include "ReadEXR.h"

#include <algorithm>

#ifdef _WIN32
#include <string>
#include <windows.h>
#include <fstream>
#include <ImfStdIO.h>
#endif

#include <ImfPixelType.h>
#include <ImfChannelList.h>
#include <ImfInputFile.h>

#ifdef OFX_IO_MT_EXR
#include <ofxsMultiThread.h>
#endif

#ifndef OPENEXR_IMF_NAMESPACE
#define OPENEXR_IMF_NAMESPACE Imf
#endif
namespace Imf_ = OPENEXR_IMF_NAMESPACE;

using std::cout; using std::endl;

static const bool kSupportsTiles = false;

namespace Exr {
    
    ////@enum A small enum to map exr components to Openfx. We can't support more as the openfx standard is limited to R,G,B and A.
    enum Channel {
        Channel_red = 0,
        Channel_green,
        Channel_blue,
        Channel_alpha,
        Channel_none
        
    };
    
    static Channel fromExrChannel(const std::string& from)
    {
        if (from == "R" ||
            from == "r" ||
            from == "Red" ||
            from == "RED" ||
            from == "red" ||
            from == "y" ||
            from == "Y") {
            return Channel_red;
        }
        if (from == "G" ||
            from == "g" ||
            from == "Green" ||
            from == "GREEN" ||
            from == "green" ||
            from == "ry" ||
            from == "RY") {
            return Channel_green;
        }
        if (from == "B" ||
            from == "b" ||
            from == "Blue" ||
            from == "BLUE" ||
            from == "blue" ||
            from == "by" ||
            from == "BY") {
            return Channel_blue;
        }
        if (from == "A" ||
            from == "a" ||
            from == "Alpha" ||
            from == "ALPHA" ||
            from == "alpha") {
            return Channel_alpha;
        }
        throw std::invalid_argument("OpenFX doesn't support the channel " + from);
        return Channel_alpha;
    }
    
    class ChannelExtractor
    {
    public:
        ChannelExtractor(const std::string& name, const std::vector<std::string>& views) :
        _mappedChannel(Channel_none),
        _valid(false)
        {     _valid = extractExrChannelName(name, views);  }
        
        ~ChannelExtractor() {}
        
        Channel _mappedChannel;
        bool _valid;
        std::string _chan;
        std::string _layer;
        std::string _view;
        
        std::string exrName() const{
            if (!_layer.empty())
                return _layer + '.' + _chan;
            return _chan;
        }
        
        bool isValid() const {return _valid;}
        
    private:
        
        
        static bool IsView(const std::string& name, const std::vector<std::string>& views)
        {
            for ( size_t i = 0; i < views.size(); ++i ){
                if ( views[i] == name ){
                    return true;
                }
            }
            
            return false;
        }
        
        
        std::vector<std::string> split(const std::string& str, char splitChar)
        {
            std::vector<std::string> out;
            size_t i = str.find(splitChar);
            size_t offset = 0;
            while ( i != str.npos ){
                out.push_back(str.substr(offset, i - offset));
                offset = i + 1;
                i = str.find(splitChar, offset);
            }
            out.push_back(str.substr(offset));
            
            return out;
        }
        
        
        std::string removePrependingDigits(const std::string& str)
        {
            std::string ret;
            unsigned int i = 0;
            while (isdigit(str[i]) && (i < str.size())) {
                ++i;
            }
            for (; i < str.size(); ++i )
                ret.push_back(str[i]);
            
            return ret;
        }
        
        std::string removeNonAlphaCharacters(const std::string& str)
        {
            std::string ret;
            for ( unsigned int i = 0; i < str.size(); i++ ){
                if (!isalnum(str[i])) {
                    ret.push_back('_');
                } else {
                    ret.push_back(str[i]);
                }
            }
            
            return ret;
        }
        
        bool extractExrChannelName(const std::string& channelname,
                                   const std::vector<std::string>& views){
            _chan.clear();
            _layer.clear();
            _view.clear();
            
            
            std::vector<std::string> splits = split(channelname, '.');
            std::vector<std::string> newSplits;
            //remove prepending digits
            for (int i = 0; i < splits.size(); ++i) {
                std::string s = removePrependingDigits(splits[i]);
                if (!s.empty()){
                    newSplits.push_back(removeNonAlphaCharacters(s));
                }
            }
            
            if (newSplits.size() > 1){
                
                for (int i = 0; i < (newSplits.size() - 1);++i) {
                    std::vector<std::string>::const_iterator foundView = std::find(views.begin(), views.end(),newSplits[i]);
                    if (foundView != views.end()) {
                        _view = *foundView;
                    } else {
                        if (!_layer.empty())
                            _layer += '_';
                        _layer += newSplits[i];
                    }
                }
                _chan = newSplits.back();
            } else {
                _chan = newSplits[0];
            }
            
            try {
                _mappedChannel = Exr::fromExrChannel(_chan);
            } catch (const std::exception& e) {
                std::cout << "Error while reading EXR file" << ": " << e.what() << std::endl;
                return false;
            }
            return true;
        }
    };
    
    struct File {
        
        File(const std::string& filename);
        
        
        ~File();
        
        Imf::InputFile* inputfile;
        
        typedef std::map<Channel, std::string> ChannelsMap;
        ChannelsMap channel_map;
        int dataOffset;
        std::vector<std::string> views;
        OfxRectI displayWindow;
        OfxRectI dataWindow;
#ifdef _WIN32
        std::ifstream* inputStr;
        Imf::StdIFStream* inputStdStream;
#endif
#ifdef OFX_IO_MT_EXR
        OFX::MultiThread::Mutex lock;
#endif
#ifdef _WIN32
        inline std::wstring s2ws(const std::string& s)
        {
            int len;
            int slength = (int)s.length() + 1;
            len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
            wchar_t* buf = new wchar_t[len];
            MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
            std::wstring r(buf);
            delete[] buf;
            return r;
        }
#endif
    };
    
    File::File(const std::string& filename)
    : inputfile(0)
    , channel_map()
    , dataOffset(0)
    , views()
    , displayWindow()
    , dataWindow()
#ifdef _WIN32
    , inputStr(0)
    , inputStdStream(0)
#endif
#ifdef OFX_IO_MT_EXR
    , lock()
#endif
    {
        
        try{
#ifdef _WIN32
            inputStr = new std::ifstream(s2ws(filename),std::ios_base::binary);
            inputStdStream = new Imf_::StdIFStream(*inputStr,filename.c_str());
            inputfile = new Imf_::InputFile(*inputStdStream);
#else
            
            inputfile = new Imf_::InputFile(filename.c_str());
#endif
        
            
            // convert exr channels to our channels
            const Imf_::ChannelList& imfchannels = inputfile->header().channels();
            
            for (Imf_::ChannelList::ConstIterator chan = imfchannels.begin(); chan != imfchannels.end(); ++chan) {
                
                std::string chanName(chan.name());
                
                ///empty channel, discard it
                if(chanName.empty()){
                    continue;
                }
                
                ///convert the channel to ours
                ChannelExtractor exrExctractor(chan.name(),views);
                
                ///if we successfully extracted the channels
                if (exrExctractor.isValid()) {
                    ///register the extracted channel
                    channel_map.insert(std::make_pair(exrExctractor._mappedChannel,exrExctractor._chan));
                }else {
                    std::cout << "Cannot decode channel " << chan.name() << std::endl;
                }
                
            }
            
            const Imath::Box2i& datawin = inputfile->header().dataWindow();
            const Imath::Box2i& dispwin = inputfile->header().displayWindow();
            Imath::Box2i formatwin(dispwin);
            formatwin.min.x = 0;
            formatwin.min.y = 0;
            dataOffset = 0;
            
            if (dispwin.min.x != 0) {
                // Shift both to get dispwindow over to 0,0.
                dataOffset = -dispwin.min.x;
                formatwin.max.x = dispwin.max.x + dataOffset;
            }
            formatwin.max.y = dispwin.max.y - dispwin.min.y;
            
            displayWindow.x1 = 0;
            displayWindow.y1 = 0;
            displayWindow.x2 = formatwin.max.x + 1;
            displayWindow.y2 = formatwin.max.y;
            
            int left = datawin.min.x + dataOffset;
            int bottom = dispwin.max.y - datawin.max.y;
            int right = datawin.max.x + dataOffset;
            int top = dispwin.max.y - datawin.min.y;
            if (datawin.min.x != dispwin.min.x || datawin.max.x != dispwin.max.x ||
                datawin.min.y != dispwin.min.y || datawin.max.y != dispwin.max.y) {
                --left;
                --bottom;
                ++right;
                ++top;
            }
            dataWindow.x1 = left;
            dataWindow.x2 = right + 1;
            dataWindow.y1 = bottom;
            dataWindow.y2 = top + 1;
            
        }catch(const std::exception& e) {
#ifdef _WIN32
            delete inputStr;
            delete inputStdStream;
#endif
            delete inputfile;
            inputfile = 0;
            throw e;
        }
        
    }
    
    File::~File(){
#ifdef _WIN32
        delete inputStr;
        delete inputStdStream;
#endif
        delete inputfile;
    }
    
    // Keeps track of all Exr::File mapped against file name.
    class FileManager
    {
        typedef std::map<std::string, File*> FilesMap;

        FilesMap _files;
        bool _isLoaded;///< register all "global" flags to ffmpeg outside of the constructor to allow
        /// all OpenFX related stuff (which depend on another singleton) to be allocated.

#ifdef OFX_IO_MT_EXR
        // internal lock
        OFX::MultiThread::Mutex *_lock;
#endif
        

        
    public:
        
        // singleton
        static FileManager s_readerManager;
        
        // constructor
        FileManager();
        
        ~FileManager();
        
        void initialize();
        
        // get a specific reader
        File* get(const std::string& filename);
        
        
    };
    
    FileManager FileManager::s_readerManager;
    
    // constructor
    FileManager::FileManager()
    : _files()
    , _isLoaded(false)
#ifdef OFX_IO_MT_EXR
    , _lock(0)
#endif
    {
    }
    
    FileManager::~FileManager() {
        for (FilesMap::iterator it = _files.begin(); it!= _files.end(); ++it) {
            delete it->second;
        }
    }
    
    void FileManager::initialize() {
        if(!_isLoaded){
#ifdef OFX_IO_MT_EXR
            _lock = new OFX::MultiThread::Mutex();
#endif
            _isLoaded = true;
        }
        
    }
    
    // get a specific reader
    File* FileManager::get(const std::string& filename)
    {
        
        assert(_isLoaded);
#ifdef OFX_IO_MT_EXR
        OFX::MultiThread::AutoMutex g(*_lock);
#endif
        FilesMap::iterator it = _files.find(filename);
        if (it == _files.end()) {
            std::pair<FilesMap::iterator,bool> ret = _files.insert(std::make_pair(std::string(filename), new File(filename)));
            assert(ret.second);
            return ret.first->second;
        }
        else {
            return it->second;
        }
    }
    
}




ReadEXRPlugin::ReadEXRPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle)
{
    Exr::FileManager::s_readerManager.initialize();
}

ReadEXRPlugin::~ReadEXRPlugin(){
    
}

void ReadEXRPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName){
    GenericReaderPlugin::changedParam(args, paramName);
}

struct DecodingChannelsMap {
    float* buf;
    bool subsampled;
    std::string channelName;
};

void ReadEXRPlugin::decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, OFX::Image* dstImg)
{
    Exr::File* file = Exr::FileManager::s_readerManager.get(filename);
    OfxRectI roi = dstImg->getRegionOfDefinition();
    assert(kSupportsTiles || (renderWindow.x1 == file->dataWindow.x1 && renderWindow.x2 == file->dataWindow.x2 && renderWindow.y1 == file->dataWindow.y1 && renderWindow.y2 == file->dataWindow.y2));

    for (int y = roi.y1; y < roi.y2; ++y) {
        
        
        std::map<Exr::Channel, DecodingChannelsMap > channels;
        for (Exr::File::ChannelsMap::const_iterator it = file->channel_map.begin(); it!=file->channel_map.end(); ++it) {
            DecodingChannelsMap d;
            
            ///This line means we only support FLOAT dst images with the RGBA format.
            d.buf = (float*)dstImg->getPixelAddress(0, y) + (int)it->first;
            
            d.subsampled = it->second == "BY" || it->second == "RY";
            d.channelName = it->second;
            channels.insert(std::make_pair(it->first, d));
        }
        
        const Imath::Box2i& dispwin = file->inputfile->header().displayWindow();
        const Imath::Box2i& datawin = file->inputfile->header().dataWindow();
        int exrY = dispwin.max.y - y;
        //int r = roi.x2;
        //int x = roi.x1;
        
        //const int X = std::max(x, datawin.min.x + file->dataOffset);
        //const int R = std::min(r, datawin.max.x + file->dataOffset + 1);
        
        // if we're below or above the data window
        if(exrY < datawin.min.y || exrY > datawin.max.y/* || R <= X*/) {
            continue;
        }
        
        Imf_::FrameBuffer fbuf;
        for(std::map<Exr::Channel,DecodingChannelsMap >::const_iterator z = channels.begin(); z != channels.end();++z){
            if(!z->second.subsampled){
                fbuf.insert(z->second.channelName.c_str(),
                            Imf_::Slice(Imf_::FLOAT,(char*)(z->second.buf /*+ file->dataOffset*/),sizeof(float) * 4, 0));
            }else{
                fbuf.insert(z->second.channelName.c_str(),
                            Imf_::Slice(Imf_::FLOAT,(char*)(z->second.buf /*+ file->dataOffset*/),sizeof(float) * 4, 0,2,2));
            }
            
        }
        {
#ifdef OFX_IO_MT_EXR
            OFX::MultiThread::AutoMutex locker(file->lock);
#endif
            try {
                file->inputfile->setFrameBuffer(fbuf);
                file->inputfile->readPixels(exrY);
            } catch (const std::exception& e) {
                setPersistentMessage(OFX::Message::eMessageError, "",std::string("OpenEXR error") + ": " + e.what());
                return;
            }
        }
        
    }
    
}


void ReadEXRPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime /*time*/,OfxRectD& rod){
    Exr::File* file = Exr::FileManager::s_readerManager.get(filename);
    rod.x1 = file->dataWindow.x1;
    rod.x2 = file->dataWindow.x2;
    rod.y1 = file->dataWindow.y1;
    rod.y2 = file->dataWindow.y2;
    
}

using namespace OFX;

#if 0
namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ReadEXRPluginFactory p("fr.inria.openfx:ReadEXR", 1, 0);
            ids.push_back(&p);
        }
    };
};
#endif

/** @brief The basic describe function, passed a plugin descriptor */
void ReadEXRPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles);
    // basic labels
    desc.setLabels("ReadEXROFX", "ReadEXROFX", "ReadEXROFX");
    desc.setPluginDescription("Read EXR images using OpenEXR.");
#ifdef OFX_IO_MT_EXR
    desc.setRenderThreadSafety(eRenderFullySafe);
#endif

#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "exr", NULL };
    desc.addSupportedExtensions(extensions);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ReadEXRPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(), /*supportsRGBA =*/ false, /*supportsRGB =*/ false, /*supportsAlpha =*/ false, /*supportsTiles =*/ kSupportsTiles);

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ReadEXRPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new ReadEXRPlugin(handle);
}


