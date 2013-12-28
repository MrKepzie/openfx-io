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
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
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

namespace {
    
    class FfmpegFile {
        
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
            
            Stream()
            : _idx(0)
            , _avstream(NULL)
            , _codecContext(NULL)
            , _videoCodec(NULL)
            , _avFrame(NULL)
            , _convertCtx(NULL)
            , _fpsNum(1)
            , _fpsDen(1)
            , _startPTS(0)
            , _frames(0)
            , _ptsSeen(false)
            , _timestampField(&AVPacket::pts)
            , _width(0)
            , _height(0)
            , _aspect(1.0)
            , _decodeNextFrameIn(-1)
            , _decodeNextFrameOut(-1)
            , _accumDecodeLatency(0)
            {}
            
            ~Stream()
            {
                
                if (_avFrame)
                    av_free(_avFrame);
                
                if (_codecContext)
                    avcodec_close(_codecContext);
                
                if (_convertCtx)
                    sws_freeContext(_convertCtx);
            }
            
            int64_t frameToPts(int frame) const
            {
                return _startPTS + (int64_t(frame) * _fpsDen *  _avstream->time_base.den) /
                (int64_t(_fpsNum) * _avstream->time_base.num);
            }
            
            int ptsToFrame(int64_t pts) const
            {
                return (int)((int64_t(pts - _startPTS) * _avstream->time_base.num *  _fpsNum) /
                             (int64_t(_avstream->time_base.den) * _fpsDen));
            }
            
            SwsContext* getConvertCtx()
            {
                if (!_convertCtx)
                    _convertCtx = sws_getContext(_width, _height, _codecContext->pix_fmt, _width, _height, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
                
                return _convertCtx;
            }
            
            // Return the number of input frames needed by this stream's codec before it can produce output. We expect to have to
            // wait this many frames to receive output; any more and a decode stall is detected.
            int getCodecDelay() const
            {
                return ((_videoCodec->capabilities & CODEC_CAP_DELAY) ? _codecContext->delay : 0) + _codecContext->has_b_frames;
            }
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
        void setError(const char* msg, const char* prefix = 0)
        {
            if (prefix) {
                _errorMsg = prefix;
                _errorMsg += msg;
            }
            else {
                _errorMsg = msg;
            }
            _invalidState = true;
        }
        
        // set FFmpeg library error
        void setInternalError(const int error, const char* prefix = 0)
        {
            char errorBuf[1024];
            av_strerror(error, errorBuf, sizeof(errorBuf));
            setError(errorBuf, prefix);
        }
        
        // get stream start time
        int64_t getStreamStartTime(Stream& stream)
        {
            
            // Read from stream. If the value read isn't valid, get it from the first frame in the stream that provides such a
            // value.
            int64_t startPTS = stream._avstream->start_time;
            
            if (startPTS ==  int64_t(AV_NOPTS_VALUE)) {
                
                // Seek 1st key-frame in video stream.
                avcodec_flush_buffers(stream._codecContext);
                
                if (av_seek_frame(_context, stream._idx, 0, 0) >= 0) {
                    av_init_packet(&_avPacket);
                    
                    // Read frames until we get one for the video stream that contains a valid PTS.
                    do {
                        if (av_read_frame(_context, &_avPacket) < 0)  {
                            // Read error or EOF. Abort search for PTS.
                            break;
                        }
                        if (_avPacket.stream_index == stream._idx) {
                            // Packet read for video stream. Get its PTS. Loop will continue if the PTS is AV_NOPTS_VALUE.
                            startPTS = _avPacket.pts;
                        }
                        
                        av_free_packet(&_avPacket);
                    } while (startPTS ==  int64_t(AV_NOPTS_VALUE));
                }
            }
            
            // If we still don't have a valid initial PTS, assume 0. (This really shouldn't happen for any real media file, as
            // it would make meaningful playback presentation timing and seeking impossible.)
            if (startPTS ==  int64_t(AV_NOPTS_VALUE)) {
                startPTS = 0;
            }
            return startPTS;
        }
        
        // Get the video stream duration in frames...
        int64_t getStreamFrames(Stream& stream)
        {
            
            int64_t frames = 0;
            
            // Obtain from movie duration if specified. This is preferred since mov/mp4 formats allow the media in
            // tracks (=streams) to be remapped in time to the final movie presentation without needing to recode the
            // underlying tracks content; the movie duration thus correctly describes the final presentation.
            if (_context->duration != 0) {
                // Annoyingly, FFmpeg exposes the movie duration converted (with round-to-nearest semantics) to units of
                // AV_TIME_BASE (microseconds in practice) and does not expose the original rational number duration
                // from a mov/mp4 file's "mvhd" atom/box. Accuracy may be lost in this conversion; a duration that was
                // an exact number of frames as a rational may end up as a duration slightly over or under that number
                // of frames in units of AV_TIME_BASE.
                // Conversion to whole frames rounds up the resulting number of frames because a partial frame is still
                // a frame. However, in an attempt to compensate for AVFormatContext's inaccurate representation of
                // duration, with unknown rounding direction, the conversion to frames subtracts 1 unit (microsecond)
                // from that duration. The rationale for this is thus:
                // * If the stored duration exactly represents an exact number of frames, then that duration minus 1
                //   will result in that same number of frames once rounded up.
                // * If the stored duration is for an exact number of frames that was rounded down, then that duration
                //   minus 1 will result in that number of frames once rounded up.
                // * If the stored duration is for an exact number of frames that was rounded up, then that duration
                //   minus 1 will result in that number of frames once rounded up, while that duration unchanged would
                //   result in 1 more frame being counted after rounding up.
                // * If the original duration in the file was not for an exact number of frames, then the movie timebase
                //   would have to be >= 10^6 for there to be any chance of this calculation resulting in the wrong
                //   number of frames. This isn't a case that I've seen. Even if that were to be the case, the original
                //   duration would have to be <= 1 microsecond greater than an exact number of frames in order to
                //   result in the wrong number of frames, which is highly improbable.
                int64_t divisor = int64_t(AV_TIME_BASE) * stream._fpsDen;
                frames = ((_context->duration - 1) * stream._fpsNum + divisor - 1) / divisor;
                
                // The above calculation is not reliable, because it seems in some situations (such as rendering out a mov
                // with 5 frames at 24 fps from Nuke) the duration has been rounded up to the nearest millisecond, which
                // leads to an extra frame being reported.  To attempt to work around this, compare against the number of
                // frames in the stream, and if they differ by one, use that value instead.
                int64_t streamFrames = stream._avstream->nb_frames;
                if( streamFrames > 0 && std::abs(frames - streamFrames) <= 1 ) {
                    frames = streamFrames;
                }
            }
            
            // If number of frames still unknown, obtain from stream's number of frames if specified. Will be 0 if
            // unknown.
            if (!frames) {
                frames = stream._avstream->nb_frames;
            }
            
            // If number of frames still unknown, attempt to calculate from stream's duration, fps and timebase.
            if (!frames) {
                frames = (int64_t(stream._avstream->duration) * stream._avstream->time_base.num  * stream._fpsNum) /
                (int64_t(stream._avstream->time_base.den) * stream._fpsDen);
                
            }
            
            // If the number of frames is still unknown, attempt to measure it from the last frame PTS for the stream in the
            // file relative to first (which we know from earlier).
            if (!frames) {
                
                int64_t maxPts = stream._startPTS;
                
                // Seek last key-frame.
                avcodec_flush_buffers(stream._codecContext);
                av_seek_frame(_context, stream._idx, stream.frameToPts(1<<29), AVSEEK_FLAG_BACKWARD);
                
                // Read up to last frame, extending max PTS for every valid PTS value found for the video stream.
                av_init_packet(&_avPacket);
                
                while (av_read_frame(_context, &_avPacket) >= 0) {
                    if (_avPacket.stream_index == stream._idx && _avPacket.pts != int64_t(AV_NOPTS_VALUE) && _avPacket.pts > maxPts)
                        maxPts = _avPacket.pts;
                    av_free_packet(&_avPacket);
                }
                
                // Compute frame range from min to max PTS. Need to add 1 as both min and max are at starts of frames, so stream
                // extends for 1 frame beyond this.
                frames = 1 + stream.ptsToFrame(maxPts);
                
            }
            return frames;
        }
        
    public:
        
        // constructor
        FfmpegFile(const std::string& filename)
        : _filename(filename)
        , _context(NULL)
        , _format(NULL)
        , _invalidState(false)
        {
            
            CHECK( avformat_open_input(&_context, filename.c_str(), _format, NULL) );
            CHECK( avformat_find_stream_info(_context, NULL) );
            
            // fill the array with all available video streams
            bool unsuported_codec = false;
            
            // find all streams that the library is able to decode
            for (unsigned i = 0; i < _context->nb_streams; ++i) {
                AVStream* avstream = _context->streams[i];
                
                // be sure to have a valid stream
                if (!avstream || !avstream->codec) {
                    continue;
                }
                
                // considering only video streams, skipping audio
                if (avstream->codec->codec_type != AVMEDIA_TYPE_VIDEO) {
                    continue;
                }
                
                // find the codec
                AVCodec* videoCodec = avcodec_find_decoder(avstream->codec->codec_id);
                if (videoCodec == NULL) {
                    continue;
                }
                
                //            // skip codec from the black list
                //            if (Foundry::Nuke::isCodecBlacklistedForReading(videoCodec->name)) {
                //                unsuported_codec = true;
                //                continue;
                //            }
                
                // skip if the codec can't be open
                if (avcodec_open2(avstream->codec, videoCodec, NULL) < 0) {
                    continue;
                }
                
                Stream* stream = new Stream();
                stream->_idx = i;
                stream->_avstream = avstream;
                stream->_codecContext = avstream->codec;
                stream->_videoCodec = videoCodec;
                stream->_avFrame = avcodec_alloc_frame();
                
                // If FPS is specified, record it.
                // Otherwise assume 1 fps (default value).
                if ( avstream->r_frame_rate.num != 0 &&  avstream->r_frame_rate.den != 0 ) {
                    stream->_fpsNum = avstream->r_frame_rate.num;
                    stream->_fpsDen = avstream->r_frame_rate.den;
                }
                
                stream->_width  = avstream->codec->width;
                stream->_height = avstream->codec->height;
                
                // set aspect ratio
                if (stream->_avstream->sample_aspect_ratio.num) {
                    stream->_aspect = av_q2d(stream->_avstream->sample_aspect_ratio);
                }
                else if (stream->_codecContext->sample_aspect_ratio.num) {
                    stream->_aspect = av_q2d(stream->_codecContext->sample_aspect_ratio);
                }
                
                // set stream start time and numbers of frames
                stream->_startPTS = getStreamStartTime(*stream);
                stream->_frames   = getStreamFrames(*stream);
                
                // save the stream
                _streams.push_back(stream);
            }
            
            if (_streams.empty())
                setError( unsuported_codec ? "unsupported codec..." : "unable to find video stream" );
        }
        
        // destructor
        ~FfmpegFile()
        {
            // force to close all resources needed for all streams
            for(unsigned int i = 0; i < _streams.size();++i){
                delete _streams[i];
            }
            
            if (_context)
                avformat_close_input(&_context);
        }
        
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
            return _streams.size();
        }
        
        // decode a single frame into the buffer thread safe
        bool decode(unsigned char* buffer, unsigned frame, unsigned streamIdx = 0)
        {
            OFX::MultiThread::AutoMutex guard(_lock);
            
            if (streamIdx >= _streams.size())
                return false;
            
            // get the stream
            Stream* stream = _streams[streamIdx];
            
            // Translate from the 1-based frames expected by Nuke to 0-based frame offsets for use in the rest of this code.
            int desiredFrame = frame - 1;
            
            // Early-out if out-of-range frame requested.
            if (desiredFrame < 0 || desiredFrame >= stream->_frames)
                return false;
            
            // Number of read retries remaining when decode stall is detected before we give up (in the case of post-seek stalls,
            // such retries are applied only after we've searched all the way back to the start of the file and failed to find a
            // successful start point for playback)..
            //
            // We have a rather annoying case with a small subset of media files in which decode latency (between input and output
            // frames) will exceed the maximum above which we detect decode stall at certain frames on the first pass through the
            // file but those same frames will decode succesfully on a second attempt. The root cause of this is not understood but
            // it appears to be some oddity of FFmpeg. While I don't really like it, retrying decode enables us to successfully
            // decode those files rather than having to fail the read.
            int retriesRemaining = 1;
            
            // Whether we have just performed a seek and are still awaiting the first decoded frame after that seek. This controls
            // how we respond when a decode stall is detected.
            //
            // One cause of such stalls is when a file contains incorrect information indicating that a frame is a key-frame when it
            // is not; a seek may land at such a frame but the decoder will not be able to start decoding until a real key-frame is
            // reached, which may be a long way in the future. Once a frame has been decoded, we will expect it to be the first frame
            // input to decode but it will actually be the next real key-frame found, leading to subsequent frames appearing as
            // earlier frame numbers and the movie ending earlier than it should. To handle such cases, when a stall is detected
            // immediately after a seek, we seek to the frame before the previous seek's landing frame, allowing us to search back
            // through the movie for a valid key frame from which decode commences correctly; if this search reaches the beginning of
            // the movie, we give up and fail the read, thus ensuring that this method will exit at some point.
            //
            // Stalls once seeking is complete and frames are being decoded are handled differently; these result in immediate read
            // failure.
            bool awaitingFirstDecodeAfterSeek = false;
            
            // If the frame we want is not the next one to be decoded, seek to the keyframe before/at our desired frame. Set the last
            // seeked frame to indicate that we need to synchronise frame indices once we've read the first frame of the video stream,
            // since we don't yet know which frame number the seek will land at. Also invalidate current indices, reset accumulated
            // decode latency and record that we're awaiting the first decoded frame after a seek.
            int lastSeekedFrame = -1; // 0-based index of the last frame to which we seeked when seek in progress / negative when no
                                      // seek in progress,
            
            if (desiredFrame != stream->_decodeNextFrameOut) {
                
                lastSeekedFrame = desiredFrame;
                stream->_decodeNextFrameIn  = -1;
                stream->_decodeNextFrameOut = -1;
                stream->_accumDecodeLatency = 0;
                awaitingFirstDecodeAfterSeek = true;
                
                avcodec_flush_buffers(stream->_codecContext);
                int error = av_seek_frame(_context, stream->_idx, stream->frameToPts(desiredFrame), AVSEEK_FLAG_BACKWARD);
                if (error < 0) {
                    // Seek error. Abort attempt to read and decode frames.
                    setInternalError(error, "FFmpeg Reader failed to seek frame: ");
                    return false;
                }
            }
            av_init_packet(&_avPacket);
            
            // Loop until the desired frame has been decoded. May also break from within loop on failure conditions where the
            // desired frame will never be decoded.
            bool hasPicture = false;
            do {
                bool decodeAttempted = false;
                int frameDecoded = 0;
                
                // If the next frame to decode is within range of frames (or negative implying invalid; we've just seeked), read
                // a new frame from the source file and feed it to the decoder if it's for the video stream.
                if (stream->_decodeNextFrameIn < stream->_frames) {
                    
                    
                    int error = av_read_frame(_context, &_avPacket);
                    if (error < 0) {
                        // Read error. Abort attempt to read and decode frames.
                        setInternalError(error, "FFmpeg Reader failed to read frame: ");
                        break;
                    }
                    
                    // If the packet read belongs to the video stream, synchronise frame indices from it if required and feed it
                    // into the decoder.
                    if (_avPacket.stream_index == stream->_idx) {
                        // If the packet read has a valid PTS, record that we have seen a PTS for this stream.
                        if (_avPacket.pts != int64_t(AV_NOPTS_VALUE))
                            stream->_ptsSeen = true;
                        
                        // If a seek is in progress, we need to synchronise frame indices if we can...
                        if (lastSeekedFrame >= 0) {
                            // Determine which frame the seek landed at, using whichever kind of timestamp is currently selected for this
                            // stream. If there's no timestamp available at that frame, we can't synchronise frame indices to know which
                            // frame we're first going to decode, so we need to seek back to an earlier frame in hope of obtaining a
                            // timestamp. Likewise, if the landing frame is after the seek target frame (this can happen, presumably a bug
                            // in FFmpeg seeking), we need to seek back to an earlier frame so that we can start decoding at or before the
                            // desired frame.
                            int landingFrame;
                            if (_avPacket.*stream->_timestampField == int64_t(AV_NOPTS_VALUE) ||
                                (landingFrame = stream->ptsToFrame(_avPacket.*stream->_timestampField)) > lastSeekedFrame) {
                                
                                // Wind back 1 frame from last seeked frame. If that takes us to before frame 0, we're never going to be
                                // able to synchronise using the current timestamp source...
                                if (--lastSeekedFrame < 0) {
                                    
                                    // If we're currently using PTSs to determine the landing frame and we've never seen a valid PTS for any
                                    // frame from this stream, switch to using DTSs and retry the read from the initial desired frame.
                                    if (stream->_timestampField == &AVPacket::pts && !stream->_ptsSeen) {
                                        stream->_timestampField = &AVPacket::dts;
                                        lastSeekedFrame = desiredFrame;
                                        
                                    }
                                    // Otherwise, failure to find a landing point isn't caused by an absence of PTSs from the file or isn't
                                    // recovered by using DTSs instead. Something is wrong with the file. Abort attempt to read and decode frames.
                                    else {
                                        
                                        setError("FFmpeg Reader failed to find timing reference frame, possible file corruption");
                                        break;
                                    }
                                }
                                
                                // Seek to the new frame. By leaving the seek in progress, we will seek backwards frame by frame until we
                                // either successfully synchronise frame indices or give up having reached the beginning of the stream.
                                
                                avcodec_flush_buffers(stream->_codecContext);
                                error = av_seek_frame(_context, stream->_idx, stream->frameToPts(lastSeekedFrame), AVSEEK_FLAG_BACKWARD);
                                if (error < 0) {
                                    // Seek error. Abort attempt to read and decode frames.
                                    setInternalError(error, "FFmpeg Reader failed to seek frame: ");
                                    break;
                                }
                            }
                            // Otherwise, we have a valid landing frame, so set that as the next frame into and out of decode and set
                            // no seek in progress.
                            else {
                                stream->_decodeNextFrameOut = stream->_decodeNextFrameIn = landingFrame;
                                lastSeekedFrame = -1;
                            }
                        }
                        
                        // If there's no seek in progress, feed this frame into the decoder.
                        if (lastSeekedFrame < 0) {
                            // Advance next frame to input.
                            ++stream->_decodeNextFrameIn;
                            
                            // Decode the frame just read. frameDecoded indicates whether a decoded frame was output.
                            decodeAttempted = true;
                            error = avcodec_decode_video2(stream->_codecContext, stream->_avFrame, &frameDecoded, &_avPacket);
                            if (error < 0) {
                                // Decode error. Abort attempt to read and decode frames.
                                setInternalError(error, "FFmpeg Reader failed to decode frame: ");
                                break;
                            }
                        }
                    }
                    
                }
                
                // If the next frame to decode is out of frame range, there's nothing more to read and the decoder will be fed
                // null input frames in order to obtain any remaining output.
                else {
                    // Obtain remaining frames from the decoder. pkt_ contains NULL packet data pointer and size at this point,
                    // required to pump out remaining frames with no more input. frameDecoded indicates whether a decoded frame
                    // was output.
                    decodeAttempted = true;
                    int error = avcodec_decode_video2(stream->_codecContext, stream->_avFrame, &frameDecoded, &_avPacket);
                    if (error < 0) {
                        // Decode error. Abort attempt to read and decode frames.
                        setInternalError(error, "FFmpeg Reader failed to decode frame: ");
                        break;
                    }
                }
                
                // If a frame was decoded, ...
                if (frameDecoded) {
                    
                    // Now that we have had a frame decoded, we know that seek landed at a valid place to start decode. Any decode
                    // stalls detected after this point will result in immediate decode failure.
                    awaitingFirstDecodeAfterSeek = false;
                    
                    // If the frame just output from decode is the desired one, get the decoded picture from it and set that we
                    // have a picture.
                    if (stream->_decodeNextFrameOut == desiredFrame) {
                        
                        AVPicture output;
                        avpicture_fill(&output, buffer, PIX_FMT_RGB24, stream->_width, stream->_height);
                        
                        sws_scale(stream->getConvertCtx(), stream->_avFrame->data, stream->_avFrame->linesize, 0,  stream->_height, output.data, output.linesize);
                        
                        hasPicture = true;
                    }
                    
                    // Advance next output frame expected from decode.
                    ++stream->_decodeNextFrameOut;
                }
                // If no frame was decoded but decode was attempted, determine whether this constitutes a decode stall and handle if so.
                else if (decodeAttempted) {
                    // Failure to get an output frame for an input frame increases the accumulated decode latency for this stream.
                    ++stream->_accumDecodeLatency;
                    
                    // If the accumulated decode latency exceeds the maximum delay permitted for this codec at this time (the delay can
                    // change dynamically if the codec discovers B-frames mid-stream), we've detected a decode stall.
                    if (stream->_accumDecodeLatency > stream->getCodecDelay()) {
                        int seekTargetFrame; // Target frame for any seek we might perform to attempt decode stall recovery.
                        
                        // Handle a post-seek decode stall.
                        if (awaitingFirstDecodeAfterSeek) {
                            // If there's anywhere in the file to seek back to before the last seek's landing frame (which can be found in
                            // stream->_decodeNextFrameOut, since we know we've not decoded any frames since landing), then set up a seek to
                            // the frame before that landing point to try to find a valid decode start frame earlier in the file.
                            if (stream->_decodeNextFrameOut > 0) {
                                seekTargetFrame = stream->_decodeNextFrameOut - 1;
                            }
                            // Otherwise, there's nowhere to seek back. If we have any retries remaining, use one to attempt the read again,
                            // starting from the desired frame.
                            else if (retriesRemaining > 0) {
                                --retriesRemaining;
                                seekTargetFrame = desiredFrame;
                            }
                            // Otherwise, all we can do is to fail the read so that this method exits safely.
                            else {
                                setError("FFmpeg Reader failed to find decode reference frame, possible file corruption");
                                break;
                            }
                        }
                        // Handle a mid-decode stall. All we can do is to fail the read so that this method exits safely.
                        else {
                            // If we have any retries remaining, use one to attempt the read again, starting from the desired frame.
                            if (retriesRemaining > 0) {
                                --retriesRemaining;
                                seekTargetFrame = desiredFrame;
                            }
                            // Otherwise, all we can do is to fail the read so that this method exits safely.
                            else {
                                setError("FFmpeg Reader detected decoding stall, possible file corruption");
                                break;
                            }
                        }
                        
                        // If we reach here, seek to the target frame chosen above in an attempt to recover from the decode stall.
                        lastSeekedFrame = seekTargetFrame;
                        stream->_decodeNextFrameIn  = -1;
                        stream->_decodeNextFrameOut = -1;
                        stream->_accumDecodeLatency = 0;
                        awaitingFirstDecodeAfterSeek = true;
                        
                        avcodec_flush_buffers(stream->_codecContext);
                        int error = av_seek_frame(_context, stream->_idx, stream->frameToPts(seekTargetFrame), AVSEEK_FLAG_BACKWARD);
                        if (error < 0) {
                            // Seek error. Abort attempt to read and decode frames.
                            setInternalError(error, "FFmpeg Reader failed to seek frame: ");
                            break;
                        }
                    }
                }
                
                av_free_packet(&_avPacket);
            } while (!hasPicture);
            
            // If read failed, reset the next frame expected out so that we seek and restart decode on next read attempt. Also free
            // the AVPacket, since it won't have been freed at the end of the above loop (we reach here by a break from the main
            // loop when hasPicture is false).
            if (!hasPicture) {
                if (_avPacket.size > 0)
                    av_free_packet(&_avPacket);
                stream->_decodeNextFrameOut = -1;
            }
            
            return hasPicture;
        }
        
        // get stream information
        bool info( int& width,
                  int& height,
                  double& aspect,
                  int& frames,
                  unsigned streamIdx = 0)
        {
            OFX::MultiThread::AutoMutex guard(_lock);
            
            if (streamIdx >= _streams.size())
                return false;
            
            // get the stream
            Stream* stream = _streams[streamIdx];
            
            width  = stream->_width;
            height = stream->_height;
            aspect = stream->_aspect;
            frames = (int)stream->_frames;
            
            return true;
        }
        
    };
    
    // Keeps track of all FFmpegFile mapped against file name.
    class FFmpegFileManager
    {
        typedef std::map<std::string, FfmpegFile*> ReaderMap;
        
        ReaderMap _readers;
        
        // internal lock
        OFX::MultiThread::Mutex *_lock;
        
        bool _isLoaded;///< register all "global" flags to ffmpeg outside of the constructor to allow
                       /// all OpenFX related stuff (which depend on another singleton) to be allocated.
        
        // A lock manager function for FFmpeg, enabling it to use mutexes managed by this reader. Pass to av_lockmgr_register().
        static int FFmpegLockManager(void** mutex, enum AVLockOp op)
        {
            switch (op) {
                case AV_LOCK_CREATE: // Create a mutex.
                    try {
                        *mutex = static_cast< void* >(new OFX::MultiThread::Mutex);
                        return 0;
                    }
                    catch(...) {
                        // Return error if mutex creation failed.
                        return 1;
                    }
                    
                case AV_LOCK_OBTAIN: // Lock the specified mutex.
                    try {
                        static_cast< OFX::MultiThread::Mutex* >(*mutex)->lock();
                        return 0;
                    }
                    catch(...) {
                        // Return error if mutex lock failed.
                        return 1;
                    }
                    
                case AV_LOCK_RELEASE: // Unlock the specified mutex.
                                      // Mutex unlock can't fail.
                    static_cast< OFX::MultiThread::Mutex* >(*mutex)->unlock();
                    return 0;
                    
                case AV_LOCK_DESTROY: // Destroy the specified mutex.
                                      // Mutex destruction can't fail.
                    delete static_cast< OFX::MultiThread::Mutex* >(*mutex);
                    *mutex = 0;
                    return 0;
                    
                default: // Unknown operation.
                    assert(false);
                    return 1;
            }
        }
        
    public:
        
        // singleton
        static FFmpegFileManager s_readerManager;
        
        // constructor
        FFmpegFileManager()
        : _lock(0)
        , _isLoaded(false)
        {
            
        }
        
        void initialize() {
            if(!_isLoaded){
                _lock = new OFX::MultiThread::Mutex();
                
                av_log_set_level(AV_LOG_WARNING);
                av_register_all();
                
                // Register a lock manager callback with FFmpeg, providing it the ability to use mutex locking around
                // otherwise non-thread-safe calls.
                av_lockmgr_register(FFmpegLockManager);
                
                _isLoaded = true;
            }

        }
        
        // get a specific reader
        FfmpegFile* get(const std::string& filename)
        {
            
            assert(_isLoaded);
            OFX::MultiThread::AutoMutex g(*_lock);
            
            ReaderMap::iterator it = _readers.find(filename);
            if (it == _readers.end()) {
                std::pair<ReaderMap::iterator,bool> ret = _readers.insert(std::make_pair(std::string(filename), new FfmpegFile(filename)));
                assert(ret.second);
                return ret.first->second;
            } 
            else {
                return it->second;
            }
        }
        
        // release a specific reader
        void release(const std::string& filename)
        {
            assert(_isLoaded);
            OFX::MultiThread::AutoMutex g(*_lock);
            ReaderMap::iterator it = _readers.find(filename);
            
            if (it != _readers.end()) {
                delete it->second;
                _readers.erase(it);
            }
        }
    };
    
    FFmpegFileManager FFmpegFileManager::s_readerManager;
    
}

#endif /* defined(__Io__FfmpegHandler__) */
