// Copyright 2020 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <packager/media/formats/dvb/dvb_sub_parser.h>

#include <string>
#include <utility>
#include <vector>

#include <absl/log/check.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <packager/media/formats/mp2t/mp2t_common.h>

namespace shaka {
namespace media {

namespace {

constexpr const uint8_t kRegionId = 7;
constexpr const uint8_t kClutId = 12;
constexpr const uint8_t kObjectId1 = 1;
constexpr const uint8_t kObjectId2 = 2;

constexpr const int64_t kNoPts = 0;

// Shared test constants for all DVB parser tests
constexpr const uint8_t kDisplayDefinitionSegment[] = {
  0x02, 0xcf,  // dds_version_number(4) | display_window_flag(1) |
               //   reserved(3) | display_width(16)
  0x02, 0x3f,  // display_height(16)
};

constexpr const uint8_t kRegionCompositionSegment[] = {
  // clang-format off
  kRegionId,  // region_id
  0x00,       // region_version_number(4) | region_fill_flag(1) | reserved(3)
  0x00, 0x30, // region_width
  0x00, 0x20, // region_height
  0x33,       // region_level_of_compatibility(3) | region_depth(3) |
              //   reserved(2)
  kClutId,    // CLUT_id
  0xff,       // region_8-bit_pixel_code
  0x00,       // region_4-bit_pixel_code(4) | region_2-bit_pixel_code(2) |
              //   reserved(2)

  // First object
  0x00, kObjectId1,  // object_id
  0x00, 0x07,        // object_type(2) | object_provider_flag(2) |
                     //   object_horizontal_position(12)
  0x00, 0x08,        // reserved(4) | object_vertical_position(12)

  // Second object
  0x00, kObjectId2,  // object_id
  0x00, 0x09,        // object_type(2) | object_provider_flag(2) |
                     //   object_horizontal_position(12)
  0x00, 0x0c,        // reserved(4) | object_vertical_position(12)
};

constexpr const uint8_t kClutDefinitionSegment[] = {
  // clang-format off
  kClutId,  // CLUT_id
  0x00,     // CLUT_version_number(4) | reserved(4)

  // First color
  0x00,  // CLUT_entry_id
  0x81,  // flags (2-bit,full-range)
  70, 141, 117, 0,
  0x00,  // CLUT_entry_id
  0x41,  // flags (4-bit,full-range)
  70, 141, 117, 0,
  0x00,  // CLUT_entry_id
  0x21,  // flags (8-bit,full-range)
  70, 141, 117, 0,

  // Second color
  0x01,  // CLUT_entry_id
  0x81,  // flags (2-bit,full-range)
  33, 134, 122, 0,
  0x01,  // CLUT_entry_id
  0x41,  // flags (4-bit,full-range)
  33, 134, 122, 0,
  0x01,  // CLUT_entry_id
  0x21,  // flags (8-bit,full-range)
  33, 134, 122, 0,

  // Third color
  0x02,  // CLUT_entry_id
  0x81,  // flags (2-bit,full-range)
  100, 128, 127, 0,
  0x02,  // CLUT_entry_id
  0x41,  // flags (4-bit,full-range)
  100, 128, 127, 0,
  0x02,  // CLUT_entry_id
  0x21,  // flags (8-bit,full-range)
  100, 128, 127, 0,
  // clang-format on
};

constexpr const uint8_t kEndOfDisplaySegment[] = {0x00};

/// @param object_id The Object ID.
/// @param pairs A vector of data_type plus data body.  Each pair should be a
///        single row.  It should be image order (i.e. not interlaced).  The
///        body is an array of strings containing binary codes (e.g. "01").
std::vector<uint8_t> GenerateObjectData(
    uint8_t object_id,
    const std::vector<std::pair<uint8_t, std::vector<std::string>>>& pairs) {
  std::vector<uint8_t> ret;
  ret.push_back(0);
  ret.push_back(object_id);
  ret.push_back(0);
  ret.insert(ret.end(), 4, 0);  // insert dummy bytes for size

  auto push_data = [&](size_t start_index) {
    const auto start_size = ret.size();
    for (size_t i = start_index; i < pairs.size(); i += 2) {
      ret.push_back(pairs[i].first);

      uint8_t temp = 0;
      uint8_t count = 0;
      for (const auto& str : pairs[i].second) {
        for (const auto& ch : str) {
          if (ch == ' ')
            continue;

          CHECK(ch == '0' || ch == '1');
          temp = (temp << 1) | (ch - '0');
          if (++count == 8) {
            ret.push_back(temp);
            temp = count = 0;
          }
        }
      }
      if (count != 0)
        ret.push_back(temp << (8 - count));
      ret.push_back(0xf0);  // end-of-line
    }
    return ret.size() - start_size;
  };

  const size_t top_size = push_data(0);
  const size_t bottom_size = push_data(1);
  CHECK(top_size <= 0xffff);
  CHECK(bottom_size <= 0xffff);
  ret[3] = (top_size >> 8) & 0xff;
  ret[4] = top_size & 0xff;
  ret[5] = (bottom_size >> 8) & 0xff;
  ret[6] = bottom_size & 0xff;

  return ret;
}

}  // namespace

class DvbSubParserTest : public testing::Test {
 protected:
  const DvbImageColorSpace* GetColorSpace(DvbSubParser* parser,
                                          uint8_t clut_id) {
    return parser->GetColorSpace(clut_id);
  }
  const DvbImageBuilder* GetImage(DvbSubParser* parser, uint8_t object_id) {
    return parser->GetImageForObject(object_id);
  }
};

TEST_F(DvbSubParserTest, TestHelper) {
  const uint8_t kResult[] = {
      // clang-format off
      0x00, 0x12, 0x00,

      0x00, 0x09,  // top-rows size
      0x00, 0x04,  // bottom-rows size

      // Top-rows
      0x30, 0x1b, 0xe9, 0x6b,
      0xf0,
      0x88, 0xc8, 0xe0,
      0xf0,

      // Bottom-rows
      0x11, 0xe6, 0xcd,
      0xf0,
      // clang-format on
  };
  std::vector<uint8_t> expected(kResult, kResult + sizeof(kResult));

  const auto actual =
      GenerateObjectData(0x12, {{0x30, {"00011011", "11101001", "01101011"}},
                                {0x11, {"11100110", "11001101"}},
                                {0x88, {"11", "00", "10", "00", "11", "10"}}});
  EXPECT_EQ(actual, expected);
}

TEST_F(DvbSubParserTest, BasicFlow) {
  // Note up to segment_length is handled by caller.
  constexpr const uint8_t kDisplayDefinitionSegment[] = {
      0x00,      // dds_version_number(4) | display_window_flag(1) | reserved(3)
      0x00, 99,  // display_width
      0x00, 99,  // display_height
  };
  constexpr const uint8_t kPageCompositionSegment[] = {
      0x02,  // page_time_out
      0x04,  // page_version_number(4) | page_state(2) | reserved(2)
      // First region
      kRegionId,   // region_id
      0x00,        // reserved
      0x00, 0x11,  // region_horizontal_address
      0x00, 0x12,  // region_vertical_address
  };
  constexpr const uint8_t kRegionCompositionSegment[] = {
      kRegionId,  // region_id
      0x08,       // region_version_number(4) | region_fill_flag(1) |
                  //   reserved(3)
      0x00, 50,   // region_width
      0x00, 50,   // region_height
      0x6c,       // region_level_of_compatibility(3) | region_depth(3) |
                  //   reserved(2)
      kClutId,    // CLUT_id
      0x02,       // region_8-bit_pixel_code,
      0x28,       // region_4-bit_pixel_code(4) | region_2-bit_pixel_code(2) |
                  //   reserved(2)

      // First object
      0x00, kObjectId1,  // object_id
      0x00, 0x07,        // object_type(2) | object_provider_flag(2) |
                         //   object_horizontal_position(12)
      0x00, 0x08,        // reserved(4) | object_vertical_position(12)

      // Second object
      0x00, kObjectId2,  // object_id
      0x00, 0x09,        // object_type(2) | object_provider_flag(2) |
                         //   object_horizontal_position(12)
      0x00, 0x0c,        // reserved(4) | object_vertical_position(12)
  };
  constexpr const uint8_t kClutDefinitionSegment[] = {
      // clang-format off
      kClutId,  // CLUT_id
      0x00,     // CLUT_version_number(4) | reserved(4)

      // First color
      0x00,  // CLUT_entry_id
      0x81,  // flags (2-bit,full-range)
      70, 141, 117, 0,
      0x00,  // CLUT_entry_id
      0x41,  // flags (4-bit,full-range)
      70, 141, 117, 0,
      0x00,  // CLUT_entry_id
      0x21,  // flags (8-bit,full-range)
      70, 141, 117, 0,

      // Second color
      0x01,  // CLUT_entry_id
      0x81,  // flags (2-bit,full-range)
      33, 134, 122, 0,
      0x01,  // CLUT_entry_id
      0x41,  // flags (4-bit,full-range)
      33, 134, 122, 0,
      0x01,  // CLUT_entry_id
      0x21,  // flags (8-bit,full-range)
      33, 134, 122, 0,

      // Third color
      0x02,  // CLUT_entry_id
      0x81,  // flags (2-bit,full-range)
      100, 128, 127, 0,
      0x02,  // CLUT_entry_id
      0x41,  // flags (4-bit,full-range)
      100, 128, 127, 0,
      0x02,  // CLUT_entry_id
      0x21,  // flags (8-bit,full-range)
      100, 128, 127, 0,
      // clang-format on
  };
  // 0 0 0 0 1 1
  // 0 1 1 1 0 0
  // 1 0 1 1 1 1
  // 0 0 0 1
  const auto kObjectData1 = GenerateObjectData(
      kObjectId1,
      {
          {0x10, {"00100100", "01", "01", "000000"}},
          {0x10, {"0001", "00100001", "000001", "000000"}},
          {0x11, {"0001", "0000 1100", "0000 1000 0001", "0000 0000"}},
          {0x11, {"0000 0001", "0001", "0000 0000"}},
      });
  // 1 1 0 0
  // 0 0 1 0
  // 1 0 0 0
  const uint8_t kObjectData2[] = {
      0x00, kObjectId2, 0x00,

      0x00, 0x0f,  // top-rows length
      0x00, 0x09,  // bottom-rows length

      0x12, 0x01,       0x01, 0x00, 0x02, 0x00, 0x00, 0xf0,  // row 0
      0x12, 0x01,       0x00, 0x03, 0x00, 0x00, 0xf0,        // row 2

      0x12, 0x00,       0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0xf0,  // row 1
  };
  constexpr const uint8_t kEndOfDisplaySegment[] = {0x00};
  auto check_image_data = [&](DvbSubParser* parser, uint8_t object_id,
                              const std::vector<uint8_t>& data) {
    const RgbaColor* pixels;
    uint16_t width, height;
    auto* color_space = GetColorSpace(parser, kClutId);
    auto* image = GetImage(parser, object_id);
    ASSERT_TRUE(image);
    ASSERT_TRUE(color_space);
    ASSERT_TRUE(image->GetPixels(&pixels, &width, &height));
    ASSERT_EQ(static_cast<size_t>(width * height), data.size());
    for (size_t y = 0; y < height; y++) {
      for (size_t x = 0; x < width; x++) {
        auto color =
            color_space->GetColor(BitDepth::k8Bit, data[x + y * width]);
        EXPECT_EQ(pixels[x + y * image->max_width()], color)
            << "Object=" << static_cast<int>(object_id) << ", X=" << x
            << ", Y=" << y;
      }
    }
  };

  DvbSubParser parser;
  std::vector<std::shared_ptr<TextSample>> samples;
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kDisplayDefinition, kNoPts,
                           kDisplayDefinitionSegment,
                           sizeof(kDisplayDefinitionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kPageComposition, kNoPts,
                           kPageCompositionSegment,
                           sizeof(kPageCompositionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kRegionComposition, kNoPts,
                           kRegionCompositionSegment,
                           sizeof(kRegionCompositionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kClutDefinition, kNoPts,
                           kClutDefinitionSegment,
                           sizeof(kClutDefinitionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kObjectData, kNoPts,
                           kObjectData1.data(), kObjectData1.size(), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kObjectData, kNoPts, kObjectData2,
                           sizeof(kObjectData2), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kEndOfDisplay, kNoPts,
                           kEndOfDisplaySegment, sizeof(kEndOfDisplaySegment),
                           &samples));

  check_image_data(&parser, kObjectId1, {0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0,
                                         1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 2, 2});
  check_image_data(&parser, kObjectId2, {1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0});

  ASSERT_TRUE(parser.Flush(&samples));
  ASSERT_EQ(samples.size(), 2u);
  for (auto& sample : samples) {
    ASSERT_TRUE(sample->settings().line);
    ASSERT_EQ(sample->settings().line->type, TextUnitType::kPercent);
    ASSERT_TRUE(sample->settings().position);
    ASSERT_EQ(sample->settings().position->type, TextUnitType::kPercent);
    ASSERT_TRUE(sample->settings().width);
    ASSERT_EQ(sample->settings().width->type, TextUnitType::kPercent);
    ASSERT_TRUE(sample->settings().height);
    ASSERT_EQ(sample->settings().height->type, TextUnitType::kPercent);

    ASSERT_FALSE(sample->body().image.empty());
  }
  // Allow in either order.
  if (samples[0]->settings().position->value == 0x1a)
    std::swap(samples[0], samples[1]);

  EXPECT_EQ(samples[0]->settings().position->value, 0x18);
  EXPECT_EQ(samples[0]->settings().line->value, 0x1a);
  EXPECT_EQ(samples[0]->settings().width->value, 6);
  EXPECT_EQ(samples[0]->settings().height->value, 4);

  EXPECT_EQ(samples[1]->settings().position->value, 0x1a);
  EXPECT_EQ(samples[1]->settings().line->value, 0x1e);
  EXPECT_EQ(samples[1]->settings().width->value, 4);
  EXPECT_EQ(samples[1]->settings().height->value, 3);
}

// Test new kCueStart/kCueEnd functionality
class DvbSubParserHeartbeatTest : public ::testing::Test {
 protected:
  static constexpr int64_t kTestPts1 = 5000;  // 5 seconds
  static constexpr int64_t kTestPts2 = 10000; // 10 seconds
  static constexpr uint8_t kTestTimeout = 30; // 30 seconds
  static constexpr uint8_t kTestPage[8] = {
    kTestTimeout, // timeout
    0x00,         // page_version_number (4 bits) + page_state (2 bits) + reserved (2 bits)
    0x10,         // page_state = 0x1 (acquisition point)
    0x00, 0x00,   // region references...
    0x00, 0x00, 0x00
  };
};

TEST_F(DvbSubParserHeartbeatTest, EndOfDisplayEmitsCueStart) {
  DvbSubParser parser;
  std::vector<std::shared_ptr<TextSample>> samples;

  // Set up minimal objects so GetSamplesAsCueStart can create samples
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kDisplayDefinition, kNoPts,
                           kDisplayDefinitionSegment, sizeof(kDisplayDefinitionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kPageComposition, kTestPts1,
                           kTestPage, sizeof(kTestPage), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kRegionComposition, kTestPts1,
                           kRegionCompositionSegment, sizeof(kRegionCompositionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kClutDefinition, kTestPts1,
                           kClutDefinitionSegment, sizeof(kClutDefinitionSegment), &samples));

  // Add some object data
  auto object_data = GenerateObjectData(kObjectId1,
    {{0x10, {"01", "10", "11", "00", "00", "01"}}});
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kObjectData, kTestPts1,
                           object_data.data(), object_data.size(), &samples));

  // Clear any existing samples from setup
  samples.clear();

  // Now EndOfDisplay should emit kCueStart
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kEndOfDisplay, kTestPts1,
                           kEndOfDisplaySegment, sizeof(kEndOfDisplaySegment), &samples));

  // Should have emitted kCueStart sample
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0]->role(), TextSampleRole::kCueStart);
  EXPECT_EQ(samples[0]->start_time(), kTestPts1);
  EXPECT_EQ(samples[0]->EndTime(), kTestPts1 + 30 * kMpeg2Timescale); // 30s placeholder
}

TEST_F(DvbSubParserHeartbeatTest, PageCompositionEmitsCueEnd) {
  DvbSubParser parser;
  std::vector<std::shared_ptr<TextSample>> samples;

  // Set up minimal content and trigger kCueStart
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kDisplayDefinition, kNoPts,
                           kDisplayDefinitionSegment, sizeof(kDisplayDefinitionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kPageComposition, kTestPts1,
                           kTestPage, sizeof(kTestPage), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kRegionComposition, kTestPts1,
                           kRegionCompositionSegment, sizeof(kRegionCompositionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kClutDefinition, kTestPts1,
                           kClutDefinitionSegment, sizeof(kClutDefinitionSegment), &samples));

  auto object_data = GenerateObjectData(kObjectId1,
    {{0x10, {"01", "10", "11", "00", "00", "01"}}});
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kObjectData, kTestPts1,
                           object_data.data(), object_data.size(), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kEndOfDisplay, kTestPts1,
                           kEndOfDisplaySegment, sizeof(kEndOfDisplaySegment), &samples));

  // Should have kCueStart
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0]->role(), TextSampleRole::kCueStart);
  samples.clear();

  // Now page composition with acquisition point should emit kCueEnd
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kPageComposition, kTestPts2,
                           kTestPage, sizeof(kTestPage), &samples));

  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0]->role(), TextSampleRole::kCueEnd);
  EXPECT_EQ(samples[0]->start_time(), kTestPts2);
  EXPECT_EQ(samples[0]->EndTime(), kTestPts2);
}

TEST_F(DvbSubParserHeartbeatTest, FlushEmitsCueEndWithTimeout) {
  DvbSubParser parser;
  std::vector<std::shared_ptr<TextSample>> samples;

  // Set up content and trigger kCueStart
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kDisplayDefinition, kNoPts,
                           kDisplayDefinitionSegment, sizeof(kDisplayDefinitionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kPageComposition, kTestPts1,
                           kTestPage, sizeof(kTestPage), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kRegionComposition, kTestPts1,
                           kRegionCompositionSegment, sizeof(kRegionCompositionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kClutDefinition, kTestPts1,
                           kClutDefinitionSegment, sizeof(kClutDefinitionSegment), &samples));

  auto object_data = GenerateObjectData(kObjectId1,
    {{0x10, {"01", "10", "11", "00", "00", "01"}}});
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kObjectData, kTestPts1,
                           object_data.data(), object_data.size(), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kEndOfDisplay, kTestPts1,
                           kEndOfDisplaySegment, sizeof(kEndOfDisplaySegment), &samples));

  // Should have kCueStart
  ASSERT_EQ(samples.size(), 1u);
  samples.clear();

  // Flush should emit kCueEnd with timeout-based end time
  ASSERT_TRUE(parser.Flush(&samples));

  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0]->role(), TextSampleRole::kCueEnd);
  int64_t expected_end = kTestPts1 + kTestTimeout * kMpeg2Timescale;
  EXPECT_EQ(samples[0]->start_time(), expected_end);
  EXPECT_EQ(samples[0]->EndTime(), expected_end);
}

TEST_F(DvbSubParserHeartbeatTest, SparseStreamGeneratesCueStartEnd) {
  // Test the complete sparse stream scenario:
  // 1. Content appears -> kCueStart
  // 2. Long gap with no content
  // 3. New content appears -> kCueEnd + new kCueStart
  DvbSubParser parser;
  std::vector<std::shared_ptr<TextSample>> samples;

  // First subtitle at 5s
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kDisplayDefinition, kNoPts,
                           kDisplayDefinitionSegment, sizeof(kDisplayDefinitionSegment), &samples));

  uint8_t page_normal[8] = {
    kTestTimeout, 0x00, 0x00, // page_state = 0x0 (normal case)
    0x00, 0x00, 0x00, 0x00, 0x00
  };
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kPageComposition, kTestPts1,
                           page_normal, sizeof(page_normal), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kRegionComposition, kTestPts1,
                           kRegionCompositionSegment, sizeof(kRegionCompositionSegment), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kClutDefinition, kTestPts1,
                           kClutDefinitionSegment, sizeof(kClutDefinitionSegment), &samples));

  auto object_data = GenerateObjectData(kObjectId1,
    {{0x10, {"01", "10", "11", "00", "00", "01"}}});
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kObjectData, kTestPts1,
                           object_data.data(), object_data.size(), &samples));
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kEndOfDisplay, kTestPts1,
                           kEndOfDisplaySegment, sizeof(kEndOfDisplaySegment), &samples));

  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0]->role(), TextSampleRole::kCueStart);
  samples.clear();

  // Long gap - no content for 300 seconds, then new subtitle clears old one
  int64_t gap_pts = kTestPts1 + 300 * kMpeg2Timescale;
  ASSERT_TRUE(parser.Parse(DvbSubSegmentType::kPageComposition, gap_pts,
                           kTestPage, sizeof(kTestPage), &samples)); // page_state 0x1 clears

  // Should emit kCueEnd for previous content
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0]->role(), TextSampleRole::kCueEnd);
  EXPECT_EQ(samples[0]->start_time(), gap_pts);
}

}  // namespace media
}  // namespace shaka
