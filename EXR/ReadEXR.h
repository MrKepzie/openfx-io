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


#ifndef __Io__exrReader__
#define __Io__exrReader__

#ifdef _WIN32
#define OPENEXR_DLL
#endif

#include "GenericReader.h"

class ReadEXRPlugin : public GenericReaderPlugin {
  
public:
    
    ReadEXRPlugin(OfxImageEffectHandle handle);
    
    virtual ~ReadEXRPlugin();
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

private:
    
    virtual bool isVideoStream(const std::string& /*filename*/) { return false; }
    
    virtual void decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, OFX::Image* dstImg);
        
    virtual void getFrameRegionOfDefinition(const std::string& /*filename*/,OfxTime time,OfxRectD& rod);
    
};

mDeclareReaderPluginFactory(ReadEXRPluginFactory, {}, {},false);

#endif /* defined(__Io__exrReader__) */
