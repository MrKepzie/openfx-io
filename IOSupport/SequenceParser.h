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

#ifndef __IO__SequenceParser__
#define __IO__SequenceParser__

#include <string>
#include <set>
#include <climits>

class FrameIndexes{
public:
    FrameIndexes(){}
    
    ~FrameIndexes(){}
    
    bool isEmpty() const {return _frames.empty();}
    
    int firstFrame() const { return _frames.empty() ? INT_MIN : *_frames.begin(); }
    
    int lastFrame() const {
        if(_frames.empty()){
            return INT_MAX;
        }else{
            std::set<int>::iterator it = _frames.end();
            --it;
            return *it;
        }
    }
    
    int size() const {return _frames.size();}
    
    /*frame index is a frame index as read in the file name.*/
    bool isInSequence(int frameIndex) const {
        return _frames.find(frameIndex) != _frames.end();
    }
    
    /*frame index is a frame index as read in the file name.*/
    bool addToSequence(int frameIndex) { return _frames.insert(frameIndex).second; }
    
    void clear() { _frames.clear(); }
    
private:
    
    std::set<int> _frames;
    
};

class SequenceParser {
    
    FrameIndexes _frameIndexes;
    
public:
    
    
    SequenceParser();
    
    ~SequenceParser();
    
    /**
     * @brief Initializes the sequence, searching within the directory containing the file given by filename.
     * It first removes all the digits from the filename to find the "common" pattern of all files in the sequences,
     * and then builds-up the sequence.
     * This throws exceptions in case the parsing went wrong.
     **/
    void initializeFromFile(const std::string& filename);
    
    /**
     * @brief Returns the first frame index of the sequence, or throws an exception if the sequence is empty.
     **/
    int firstFrame() const;
    
    /**
     * @brief Returns the last frame index of the sequence, or throws an exception if the sequence is empty.
     **/
    int lastFrame() const;
    
    bool isEmpty() const;
    
private:
    
    static void getDirectoryContainerPath(const std::string& filename,std::string& dirPath);
    
    static void removePath(std::string& filename);
    
    static void parseFileName(const std::string& filename,bool filenameHasPath,int& frameNumber,std::string& commonPart,std::string& extension);
    
    static void fillSequence(const std::string& dirPath,const std::string& commonPart,const std::string& extensions,FrameIndexes& frameIndexes);
    
    static bool isPartOfSequence(const std::string& filenameWithoutPath,const std::string& commonPart,const std::string& extension,int& frameNumber);
};

#endif /* defined(__IO__SequenceParser__) */
