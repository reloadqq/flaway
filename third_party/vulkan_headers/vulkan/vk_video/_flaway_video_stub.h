/*
 * Master stub for the Vulkan StdVideo* types referenced by vulkan_core.h.
 * This is harness-only (syntax check); the real headers ship with the
 * official Vulkan SDK.
 */
#ifndef FLAWAY_VIDEO_STUB_H_
#define FLAWAY_VIDEO_STUB_H_

typedef unsigned int StdVideoH264LevelIdc;
typedef unsigned int StdVideoH264ProfileIdc;
typedef unsigned int StdVideoH265LevelIdc;
typedef unsigned int StdVideoH265ProfileIdc;
typedef unsigned int StdVideoAV1Level;
typedef unsigned int StdVideoAV1Profile;
typedef unsigned int StdVideoVP9Level;
typedef unsigned int StdVideoVP9Profile;

typedef struct { } StdVideoH264SequenceParameterSet;
typedef struct { } StdVideoH264PictureParameterSet;
typedef struct { } StdVideoH265VideoParameterSet;
typedef struct { } StdVideoH265SequenceParameterSet;
typedef struct { } StdVideoH265PictureParameterSet;
typedef struct { } StdVideoEncodeH264SliceHeader;
typedef struct { } StdVideoEncodeH264PictureInfo;
typedef struct { } StdVideoEncodeH264ReferenceInfo;
typedef struct { } StdVideoEncodeH265SliceSegmentHeader;
typedef struct { } StdVideoEncodeH265PictureInfo;
typedef struct { } StdVideoEncodeH265ReferenceInfo;
typedef struct { } StdVideoDecodeH264PictureInfo;
typedef struct { } StdVideoDecodeH264ReferenceInfo;
typedef struct { } StdVideoDecodeH265PictureInfo;
typedef struct { } StdVideoDecodeH265ReferenceInfo;
typedef struct { } StdVideoAV1SequenceHeader;
typedef struct { } StdVideoDecodeAV1PictureInfo;
typedef struct { } StdVideoDecodeAV1ReferenceInfo;
typedef struct { } StdVideoEncodeAV1DecoderModelInfo;
typedef struct { } StdVideoEncodeAV1OperatingPointInfo;
typedef struct { } StdVideoEncodeAV1PictureInfo;
typedef struct { } StdVideoEncodeAV1ReferenceInfo;
typedef struct { } StdVideoDecodeVP9PictureInfo;

#endif /* _FLAWAY_VIDEO_STUB_H_ */