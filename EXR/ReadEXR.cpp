/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2015 INRIA
 *
 * openfx-io is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-io is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-io.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX exr Reader plugin.
 * Reads a an input image using the OpenEXR library.
 */

#include <algorithm>
#ifdef DEBUG
#include <iostream>
#endif

#ifdef _WIN32
#include <string>
#include <windows.h>
#include <fstream>
#ifndef __MINGW32__
#include <ImfStdIO.h>
#endif
#endif

#include <ImfPixelType.h>
#include <ImfChannelList.h>
#include <ImfInputFile.h>
#include <IlmThreadPool.h>

#ifdef OFX_IO_MT_EXR
#include <ofxsMultiThread.h>
#endif

#include "GenericOCIO.h"
#include "GenericReader.h"


using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ReadEXR"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription "Read images using OpenEXR."
#define kPluginIdentifier "fr.inria.openfx.ReadEXR"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 10

#ifndef OPENEXR_IMF_NAMESPACE
#define OPENEXR_IMF_NAMESPACE Imf
#endif
namespace Imf_ = OPENEXR_IMF_NAMESPACE;

#define kSupportsRGBA true
#define kSupportsRGB false
#define kSupportsAlpha false
#define kSupportsTiles false

class ReadEXRPlugin : public GenericReaderPlugin
{
public:

    ReadEXRPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions);

    virtual ~ReadEXRPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:

    virtual bool isVideoStream(const std::string& /*filename*/) OVERRIDE FINAL { return false; }

    virtual void decode(const std::string& filename, OfxTime time, int /*view*/, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes) OVERRIDE FINAL;

    virtual bool getFrameBounds(const std::string& /*filename*/,OfxTime time, OfxRectI *bounds, double *par, std::string *error, int* tile_width, int* tile_height) OVERRIDE FINAL;
    
    virtual void onInputFileChanged(const std::string& newFile, bool setColorSpace, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components, int *componentCount) OVERRIDE FINAL;
};

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
            for (size_t i = 0; i < splits.size(); ++i) {
                std::string s = removePrependingDigits(splits[i]);
                if (!s.empty()){
                    newSplits.push_back(removeNonAlphaCharacters(s));
                }
            }
            
            if (newSplits.size() > 1){
                
                for (size_t i = 0; i < (newSplits.size() - 1);++i) {
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
#              ifdef DEBUG
                std::cout << "Error while reading EXR file" << ": " << e.what() << std::endl;
#              endif
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
        float pixelAspectRatio;
#if defined(_WIN32) && !defined(__MINGW32__)
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
    , pixelAspectRatio(1.)
#if defined(_WIN32) && !defined(__MINGW32__)
    , inputStr(0)
    , inputStdStream(0)
#endif
#ifdef OFX_IO_MT_EXR
    , lock()
#endif
    {
        
        try{
#if defined(_WIN32) && !defined(__MINGW32__)
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
                } else {
#                 ifdef DEBUG
                    std::cout << "Cannot decode channel " << chan.name() << std::endl;
#                 endif
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

            pixelAspectRatio = inputfile->header().pixelAspectRatio();
        }catch(const std::exception& e) {
#if defined(_WIN32) && !defined(__MINGW32__)
            delete inputStr;
            delete inputStdStream;
#endif
            delete inputfile;
            inputfile = 0;
            throw e;
        }
    }
    
    File::~File(){
#if defined(_WIN32) && !defined(__MINGW32__)
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
#ifdef OFX_IO_MT_EXR
        delete _lock;
#endif
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




ReadEXRPlugin::ReadEXRPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions)
: GenericReaderPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles, false)
{
    Exr::FileManager::s_readerManager.initialize();
}

ReadEXRPlugin::~ReadEXRPlugin(){
    
}

void
ReadEXRPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                            const std::string &paramName)
{
    GenericReaderPlugin::changedParam(args, paramName);
}

struct DecodingChannelsMap {
    float* buf;
    bool subsampled;
    std::string channelName;
};

void
ReadEXRPlugin::decode(const std::string& filename,
                      OfxTime /*time*/,
                      int /*view*/,
                      bool /*isPlayback*/,
                      const OfxRectI& renderWindow,
                      float *pixelData,
                      const OfxRectI& bounds,
                      OFX::PixelComponentEnum pixelComponents,
                      int pixelComponentCount,
                      int rowBytes)
{
    /// we only support RGBA output clip
    if (pixelComponents != OFX::ePixelComponentRGBA || pixelComponentCount != 4) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    Exr::File* file = Exr::FileManager::s_readerManager.get(filename);
    OfxRectI roi = bounds; // used to be dstImg->getRegionOfDefinition(); why?
    assert(kSupportsTiles || (renderWindow.x1 == file->dataWindow.x1 && renderWindow.x2 == file->dataWindow.x2 && renderWindow.y1 == file->dataWindow.y1 && renderWindow.y2 == file->dataWindow.y2));

    for (int y = roi.y1; y < roi.y2; ++y) {
        
        
        std::map<Exr::Channel, DecodingChannelsMap > channels;
        for (Exr::File::ChannelsMap::const_iterator it = file->channel_map.begin(); it!=file->channel_map.end(); ++it) {
            DecodingChannelsMap d;
            
            ///This line means we only support FLOAT dst images with the RGBA format.
            d.buf = (float*)((char*)pixelData + (y-roi.y1)*rowBytes) + (int)it->first;
            
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
        if (exrY < datawin.min.y || exrY > datawin.max.y/* || R <= X*/) {
            continue;
        }
        
        Imf_::FrameBuffer fbuf;
        for (std::map<Exr::Channel,DecodingChannelsMap >::const_iterator z = channels.begin(); z != channels.end();++z) {
            if( !z->second.subsampled) {
                fbuf.insert(z->second.channelName.c_str(),
                            Imf_::Slice(Imf_::FLOAT,(char*)(z->second.buf /*+ file->dataOffset*/),sizeof(float) * 4, 0));
            } else {
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

void
ReadEXRPlugin::onInputFileChanged(const std::string& newFile,
                                  bool setColorSpace, //!< true is colorspace was not set from the filename
                                  OFX::PreMultiplicationEnum *premult,
                                  OFX::PixelComponentEnum *components,
                                  int *componentCount)
{
    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        // Unless otherwise specified, exr files are assumed to be linear.
        _ocio->setInputColorspace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
#     endif
    }
    assert(premult && components);
    Exr::File* file = Exr::FileManager::s_readerManager.get(newFile);
    bool hasRed;
    bool hasGreen;
    bool hasBlue;
    bool hasAlpha;
    
    hasRed = file->channel_map.find(Exr::Channel_red) != file->channel_map.end();
    hasGreen = file->channel_map.find(Exr::Channel_green) != file->channel_map.end();
    hasBlue = file->channel_map.find(Exr::Channel_blue) != file->channel_map.end();
    hasAlpha = file->channel_map.find(Exr::Channel_alpha) != file->channel_map.end();
    
    if (hasAlpha) {
        // if any color channel is present, let it be RGBA
        if (hasRed || hasGreen || hasBlue) {
            *components = OFX::ePixelComponentRGBA;
            *componentCount = 4;
        } else {
            *components = OFX::ePixelComponentAlpha;
            *componentCount = 1;
        }
    } else {
        // if any color channel is present, let it be RGB
        if (hasRed || hasGreen || hasBlue) {
            *components = OFX::ePixelComponentRGB;
            *componentCount = 3;
        } else {
            *components = OFX::ePixelComponentNone;
            *componentCount = 0;
        }
    }
#pragma message WARN("This is probably wrong, I just set it for the sake of making it compile.")
    // where can we get premult information in EXR data?
    *premult = OFX::eImagePreMultiplied;

}

bool
ReadEXRPlugin::getFrameBounds(const std::string& filename,
                              OfxTime /*time*/,
                              OfxRectI *bounds,
                              double *par,
                              std::string *error,
                              int* tile_width,
                              int* tile_height)
{
    assert(bounds && par);
    Exr::File* file = Exr::FileManager::s_readerManager.get(filename);
    if (!file) {
        if (error) {
            *error = "No such file";
        }
        return false;
    }
    bounds->x1 = file->dataWindow.x1;
    bounds->x2 = file->dataWindow.x2;
    bounds->y1 = file->dataWindow.y1;
    bounds->y2 = file->dataWindow.y2;
    *par = file->pixelAspectRatio;
    *tile_width = *tile_height = 0;

    return true;
}


mDeclareReaderPluginFactory(ReadEXRPluginFactory, ;, false);

void
ReadEXRPluginFactory::unload()
{
    //Kill all threads
    IlmThread::ThreadPool::globalThreadPool().setNumThreads(0);
}

void
ReadEXRPluginFactory::load()
{
    _extensions.clear();
    _extensions.push_back("exr");
}

/** @brief The basic describe function, passed a plugin descriptor */
void
ReadEXRPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, _extensions, kPluginEvaluation, kSupportsTiles, false);
    // basic labels
    desc.setLabel("ReadEXROFX");
    desc.setPluginDescription("Read EXR images using OpenEXR.");
#ifdef OFX_IO_MT_EXR
    desc.setRenderThreadSafety(eRenderFullySafe);
#endif

    desc.setIsDeprecated(true); // This plugin was superseeded by ReadOIIO
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
ReadEXRPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles, true);

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect*
ReadEXRPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    ReadEXRPlugin* ret =  new ReadEXRPlugin(handle, _extensions);
    ret->restoreStateFromParameters();
    return ret;
}


static ReadEXRPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
