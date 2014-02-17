/*
 SequenceParser is a small class that helps reading a sequence of images within a directory.

 
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
#include "SequenceParser.h"

#include <stdexcept>
#include <cassert>
#include <cstdlib>
#include <iostream>

#include "tinydir/tinydir.h"

SequenceParser::SequenceParser()
{
    
}

SequenceParser::~SequenceParser() {
    
}

void SequenceParser::initializeFromFile(const std::string& filename) {
    
    ///clear the previous indexes
    _frameIndexes.clear();
    
    ///extract the common part of the filenames and the extension
    std::string commonPart,extension;
    int frameNumber;
    parseFileName(filename,true,frameNumber, commonPart, extension);
    ///if we reach here (i.e: the extractCommonPartFromInitialName didn't throw an exception)
    ///and if the commonPart is empty, that means the filename is only constituted of digits
    ///in which case we just add a filename at time 0.
    if (commonPart.empty()) {
        _frameIndexes.addToSequence(frameNumber);
        return;
    }
    
    ///get the path of the directory containing this file
    std::string dirPath;
    getDirectoryContainerPath(filename, dirPath);
    
    ///default case
    fillSequence(dirPath,commonPart, extension, _frameIndexes);
}

void SequenceParser::parseFileName(const std::string& filename,bool filenameHasPath,int& frameNumber,
                                   std::string& commonPart,std::string& extension) {
    
    size_t pos = filename.find_last_of('.');
    
    ///there's no '.' character separator at the end of the filename, just return, we can't parse this file.
    if (pos == std::string::npos) {
        throw std::invalid_argument("Cannot parse " + filename + ", the file has no '.' separator.");
    }
    
    ///the starting position of the extension
    size_t extensionPos = pos + 1;
    
    std::string frameNumberStr;
    
    ///place the cursor at the last thereotical digit
    --pos;
    
    ///extract the digits
    while (pos > 0 && std::isdigit(filename[pos])) {
        frameNumberStr.insert(frameNumberStr.begin(), filename[pos]);
        --pos;
    }
    
    ///if the filename is not composed only of digits, we have the common part
    ///For filenames composed only of digits, the commonPart string will be empty
    if (pos != 0) {
        ++pos;
        std::string filenameCopy = filename;
        commonPart = filename.substr(0,pos);
        if (filenameHasPath) {
            removePath(commonPart);
        }
        frameNumber = std::atoi(frameNumberStr.c_str());
    } else {
        ///the filename is only composed of digits, set the frame number to 0
        frameNumber = 0;
    }
    
    ///now extract the extension
    while(extensionPos < filename.size()){
        extension.push_back(filename[extensionPos]);
        ++extensionPos;
    }

}

void SequenceParser::getDirectoryContainerPath(const std::string& filename,std::string& dirPath) {
    size_t pos = filename.find_last_of('/');
    
    ///re-try again for '\\'
    if (pos == std::string::npos) {
        pos = filename.find_last_of('\\');
    }
    
    ///there's no '/' or '\' character separator just return, we can't parse this file.
    if (pos == std::string::npos) {
        throw std::invalid_argument("Cannot parse " + filename + ", the file has no '/' or '\' separator.");
    }
    
    dirPath = filename.substr(0,pos);
}

void SequenceParser::removePath(std::string& filename) {
    size_t pos = filename.find_last_of('/');
    
    ///re-try again for '\\'
    if (pos == std::string::npos) {
        pos = filename.find_last_of('\\');
    }
    
    ///there's no '/' or '\' character separator just return, we can't parse this file.
    if (pos == std::string::npos) {
        throw std::invalid_argument("Cannot parse " + filename + ", the file has no '/' or '\' separator.");
    }
    filename = filename.substr(pos+1,filename.size() - pos - 1);
}

bool SequenceParser::isPartOfSequence(const std::string& filenameWithoutPath,const std::string& commonPart,
                                      const std::string& extension,int& frameNumber) {
    size_t foundCommonPart = filenameWithoutPath.find(commonPart);
    if (foundCommonPart == std::string::npos) {
        return false;
    }
    ///okay we found the common part, now check that the file has the same extension
    
    size_t foundSameExt = filenameWithoutPath.find('.' + extension);
    if (foundSameExt == std::string::npos) {
        return false;
    }
    
    ///okay we found a valid filename, now get its frame number
    size_t pos = filenameWithoutPath.find_last_of('.');
    ///place the cursor at the last thereotical digit
    --pos;
    
    std::string frameNumberStr;
    ///extract the digits
    while (pos > 0 && std::isdigit(filenameWithoutPath[pos])) {
        frameNumberStr.insert(frameNumberStr.begin(), filenameWithoutPath[pos]);
        --pos;
    }
    
    
    frameNumber = std::atoi(frameNumberStr.c_str());

    return true;
    
}

void SequenceParser::fillSequence(const std::string& dirPath,const std::string& commonPart,
                                  const std::string& extensions,FrameIndexes& frameIndexes) {
    tinydir_dir dir;
    if (tinydir_open(&dir, dirPath.c_str()) != -1) {
        ///we successfully opened the directory
        
        ///iterate through all the files in the directory
        while (dir.has_next) {
            tinydir_file file;
            tinydir_readfile(&dir, &file);
            
            if (file.is_dir) {
                tinydir_next(&dir);
                continue;
            }
            
            std::string filename(file.name);
            int frameNumber;
            bool ret = isPartOfSequence(filename, commonPart, extensions, frameNumber);
            if (ret) {
                frameIndexes.addToSequence(frameNumber);
            }
            
            tinydir_next(&dir);

        }
        
        ///don't forget to close the directory
        tinydir_close(&dir);
    } else {
        ///couldn't open
        throw std::invalid_argument("No such directory:" + dirPath);
    }
}

/**
 * @brief Returns the first frame index of the sequence, or throws an exception if the sequence is empty.
 **/
int SequenceParser::firstFrame() const {
    int f = _frameIndexes.firstFrame();
    if (f == INT_MIN) {
        throw std::runtime_error("File sequence empty!");
    }
    return f;
}

/**
 * @brief Returns the last frame index of the sequence, or throws an exception if the sequence is empty.
 **/
int SequenceParser::lastFrame() const {
    int f = _frameIndexes.lastFrame();
    if (f == INT_MAX) {
        throw std::runtime_error("File sequence empty!");
    }
    return f;

}

