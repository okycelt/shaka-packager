// Copyright 2020 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_DVB_DVB_SUB_PARSER_H_
#define PACKAGER_MEDIA_DVB_DVB_SUB_PARSER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include <packager/media/base/bit_reader.h>
#include <packager/media/base/text_sample.h>
#include <packager/media/formats/dvb/dvb_image.h>
#include <packager/media/formats/dvb/subtitle_composer.h>

namespace shaka {
namespace media {

// See ETSI EN 300 743 Section 7.2.0.1 and Table 7.
enum class DvbSubSegmentType : uint16_t {
  kPageComposition = 0x10,
  kRegionComposition = 0x11,
  kClutDefinition = 0x12,
  kObjectData = 0x13,
  kDisplayDefinition = 0x14,
  kDisparitySignalling = 0x15,
  kAlternativeClut = 0x16,
  kEndOfDisplay = 0x80,
};

// Default timeout for DVB subtitle cues: 3 seconds at 90kHz timescale.
constexpr int64_t kDvbSubtitleTimeoutTicks = 270000;

class DvbSubParser {
 public:
  DvbSubParser();
  ~DvbSubParser();

  DvbSubParser(const DvbSubParser&) = delete;
  DvbSubParser& operator=(const DvbSubParser&) = delete;

  bool Parse(DvbSubSegmentType segment_type,
             int64_t pts,
             const uint8_t* payload,
             size_t size,
             std::vector<std::shared_ptr<TextSample>>* samples);
  bool Flush(std::vector<std::shared_ptr<TextSample>>* samples);

  // Check if pending cue has timed out and emit kCueEnd if needed
  bool CheckForTimeout(int64_t pts, std::vector<std::shared_ptr<TextSample>>* samples);

 private:
  friend class DvbSubParserTest;

  const DvbImageColorSpace* GetColorSpace(uint8_t clut_id);
  const DvbImageBuilder* GetImageForObject(uint16_t object_id);

  bool ParsePageComposition(int64_t pts,
                            const uint8_t* data,
                            size_t size,
                            std::vector<std::shared_ptr<TextSample>>* samples);
  bool ParseRegionComposition(const uint8_t* data, size_t size);
  bool ParseClutDefinition(const uint8_t* data, size_t size);
  bool ParseObjectData(int64_t pts, const uint8_t* data, size_t size);
  bool ParseDisplayDefinition(const uint8_t* data, size_t size);

  bool ParsePixelDataSubObject(size_t sub_object_length,
                               bool is_top_fields,
                               BitReader* reader,
                               DvbImageColorSpace* color_space,
                               DvbImageBuilder* image);
  bool Parse2BitPixelData(bool is_top_fields,
                          BitReader* reader,
                          DvbImageBuilder* image);
  bool Parse4BitPixelData(bool is_top_fields,
                          BitReader* reader,
                          DvbImageBuilder* image);
  bool Parse8BitPixelData(bool is_top_fields,
                          BitReader* reader,
                          DvbImageBuilder* image);

  // Helper method to emit kCueEnd when timeout expires
  void EmitTimeoutCueEnd(std::vector<std::shared_ptr<TextSample>>* samples);

  SubtitleComposer composer_;
  int64_t last_pts_;
  uint8_t timeout_;

  // State tracking for kCueStart/kCueEnd pattern
  bool has_pending_cue_ = false;  // True after kCueStart emitted
  int64_t pending_cue_pts_ = 0;   // PTS when kCueStart was emitted
  int64_t pending_cue_timeout_ = 0;  // PTS when pending cue should timeout
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_DVB_DVB_SUB_PARSER_H_
