/*
 OFX ffmpegReader plugin.
 Reads a video input file using the libav library.
 
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


#ifndef __Io__FfmpegHandler__
#define __Io__FfmpegHandler__

#include <vector>
#include <string>

extern "C" {
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

#include "ofxsMultiThread.h"

#define CHECK(x) \
{\
int error = x;\
if (error<0) {\
setInternalError(error);\
return;\
}\
}\


namespace FFmpeg {
    
    void supportedFileFormats(std::vector<std::string>* formats);
    
    bool isImageFile(const std::string& filename);
    
    
    class File;
    // Keeps track of all FFmpegFile mapped against file name.
    class FileManager
    {
        typedef std::map<std::string, File*> FilesMap;
        FilesMap _files;
        
        // internal lock
        OFX::MultiThread::Mutex *_lock;
        
        bool _isLoaded;///< register all "global" flags to ffmpeg outside of the constructor to allow
                       /// all OpenFX related stuff (which depend on another singleton) to be allocated.
        
        // A lock manager function for FFmpeg, enabling it to use mutexes managed by this reader. Pass to av_lockmgr_register().
        static int FFmpegLockManager(void** mutex, enum AVLockOp op);
        
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

    
    class File {
        
        struct Stream
        {
            int _idx;                      // stream index
            AVStream* _avstream;           // video stream
            AVCodecContext* _codecContext; // video codec context
            AVCodec* _videoCodec;
            AVFrame* _avFrame;             // decoding frame
            SwsContext* _convertCtx;
            
            int _fpsNum;
            int _fpsDen;
            
            int64_t _startPTS;     // PTS of the first frame in the stream
            int64_t _frames;       // video duration in frames
            
            bool _ptsSeen;                      // True if a read AVPacket has ever contained a valid PTS during this stream's decode,
                                                // indicating that this stream does contain PTSs.
            int64_t AVPacket::*_timestampField; // Pointer to member of AVPacket from which timestamps are to be retrieved. Enables
                                                // fallback to using DTSs for a stream if PTSs turn out not to be available.
            
            int _width;
            int _height;
            double _aspect;
            
            int _decodeNextFrameIn; // The 0-based index of the next frame to be fed into decode. Negative before any
                                    // frames have been decoded or when we've just seeked but not yet found a relevant frame. Equal to
                                    // frames_ when all available frames have been fed into decode.
            
            int _decodeNextFrameOut; // The 0-based index of the next frame expected out of decode. Negative before
                                     // any frames have been decoded or when we've just seeked but not yet found a relevant frame. Equal to
                                     // frames_ when all available frames have been output from decode.
            
            int _accumDecodeLatency; // The number of frames that have been input without any frame being output so far in this stream
                                     // since the last seek. This is part of a guard mechanism to detect when decode appears to have
                                     // stalled and ensure that FFmpegFile::decode() does not loop indefinitely.
            
            Stream();
            
            ~Stream();
            
            int64_t frameToPts(int frame) const;
            
            int ptsToFrame(int64_t pts) const;
            
            SwsContext* getConvertCtx();
            
            // Return the number of input frames needed by this stream's codec before it can produce output. We expect to have to
            // wait this many frames to receive output; any more and a decode stall is detected.
            int getCodecDelay() const;
            
        };
        
        std::string _filename;
        
        // AV structure
        AVFormatContext* _context;
        AVInputFormat*   _format;
        
        // store all video streams available in the file
        std::vector<Stream*> _streams;
        
        // reader error state
        std::string _errorMsg;  // internal decoding error string
        bool _invalidState;     // true if the reader is in an invalid state
        
        AVPacket _avPacket;
        
        // internal lock for multithread access
        OFX::MultiThread::Mutex _lock;
        
        // set reader error
        void setError(const char* msg, const char* prefix = 0);
        
        // set FFmpeg library error
        void setInternalError(const int error, const char* prefix = 0);
        
        // get stream start time
        int64_t getStreamStartTime(Stream& stream);
        
        // Get the video stream duration in frames...
        int64_t getStreamFrames(Stream& stream);
        
        friend class FileManager;
        // private constructor used by the FileManager
        File(const std::string& filename);
        
    public:
        
        ~File();
        
        const std::string& filename() const { return _filename; }
        
        // get the internal error string
        const char* error() const
        {
            return _errorMsg.c_str();
        }
        
        // return true if the reader can't decode the frame
        bool invalid() const
        {
            return _invalidState;
        }
        
        // return the numbers of streams supported by the reader
        unsigned int streams() const
        {
            return (unsigned int)_streams.size();
        }
        
        // decode a single frame into the buffer thread safe
        bool decode(unsigned char* buffer, int frame,bool loadNearest, unsigned streamIdx = 0);
        
        // get stream information
        bool info( int& width,
                  int& height,
                  double& aspect,
                  int& frames,
                  unsigned streamIdx = 0);
        
    };
    
} //namespace FFmpeg


#endif /* defined(__Io__FfmpegHandler__) */
