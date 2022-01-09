// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/video_color_space.h"

namespace media {

VideoColorSpace::PrimaryID VideoColorSpace::GetPrimaryID(int primary) {
  if (primary < 1 || primary > 22 || primary == 3)
    return PrimaryID::INVALID;
  if (primary > 12 && primary < 22)
    return PrimaryID::INVALID;
  return static_cast<PrimaryID>(primary);
}

VideoColorSpace::TransferID VideoColorSpace::GetTransferID(int transfer) {
  if (transfer < 1 || transfer > 18 || transfer == 3)
    return TransferID::INVALID;
  return static_cast<TransferID>(transfer);
}

VideoColorSpace::MatrixID VideoColorSpace::GetMatrixID(int matrix) {
  if (matrix < 0 || matrix > 11 || matrix == 3)
    return MatrixID::INVALID;
  return static_cast<MatrixID>(matrix);
}

VideoColorSpace::VideoColorSpace() = default;

VideoColorSpace::VideoColorSpace(PrimaryID primaries,
                                 TransferID transfer,
                                 MatrixID matrix,
                                 gfx::ColorSpace::RangeID range)
    : primaries(primaries), transfer(transfer), matrix(matrix), range(range) {}

VideoColorSpace::VideoColorSpace(int primaries,
                                 int transfer,
                                 int matrix,
                                 gfx::ColorSpace::RangeID range)
    : primaries(GetPrimaryID(primaries)),
      transfer(GetTransferID(transfer)),
      matrix(GetMatrixID(matrix)),
      range(range) {}

bool VideoColorSpace::operator==(const VideoColorSpace& other) const {
  return primaries == other.primaries && transfer == other.transfer &&
         matrix == other.matrix && range == other.range;
}

bool VideoColorSpace::operator!=(const VideoColorSpace& other) const {
  return primaries != other.primaries || transfer != other.transfer ||
         matrix != other.matrix || range != other.range;
}

bool VideoColorSpace::IsSpecified() const {
  if (primaries != PrimaryID::INVALID && primaries != PrimaryID::UNSPECIFIED)
    return true;
  if (transfer != TransferID::INVALID && transfer != TransferID::UNSPECIFIED)
    return true;
  if (matrix != MatrixID::INVALID && matrix != MatrixID::UNSPECIFIED)
    return true;
  // Note that it's not enough to have a range for a video color space to
  // be considered valid, because often the range is just specified with
  // a bool, so there is no way to know if it was set specifically or not.
  return false;
}

gfx::ColorSpace VideoColorSpace::ToGfxColorSpace() const {
  gfx::ColorSpace::PrimaryID primary_id = gfx::ColorSpace::PrimaryID::INVALID;
  gfx::ColorSpace::TransferID transfer_id =
      gfx::ColorSpace::TransferID::INVALID;
  gfx::ColorSpace::MatrixID matrix_id = gfx::ColorSpace::MatrixID::INVALID;

  // Bitfield, note that guesses with higher values take precedence over
  // guesses with lower values.
  enum Guess {
    GUESS_BT709 = 1 << 4,
    GUESS_BT470M = 1 << 3,
    GUESS_BT470BG = 1 << 2,
    GUESS_SMPTE170M = 1 << 1,
    GUESS_SMPTE240M = 1 << 0,
  };

  uint32_t guess = 0;

  switch (primaries) {
    case PrimaryID::BT709:
      primary_id = gfx::ColorSpace::PrimaryID::BT709;
      guess |= GUESS_BT709;
      break;
    case PrimaryID::BT470M:
      primary_id = gfx::ColorSpace::PrimaryID::BT470M;
      guess |= GUESS_BT470M;
      break;
    case PrimaryID::BT470BG:
      primary_id = gfx::ColorSpace::PrimaryID::BT470BG;
      guess |= GUESS_BT470BG;
      break;
    case PrimaryID::SMPTE170M:
      primary_id = gfx::ColorSpace::PrimaryID::SMPTE170M;
      guess |= GUESS_SMPTE170M;
      break;
    case PrimaryID::SMPTE240M:
      primary_id = gfx::ColorSpace::PrimaryID::SMPTE240M;
      guess |= GUESS_SMPTE240M;
      break;
    case PrimaryID::FILM:
      primary_id = gfx::ColorSpace::PrimaryID::FILM;
      break;
    case PrimaryID::BT2020:
      primary_id = gfx::ColorSpace::PrimaryID::BT2020;
      break;
    case PrimaryID::SMPTEST428_1:
      primary_id = gfx::ColorSpace::PrimaryID::SMPTEST428_1;
      break;
    case PrimaryID::SMPTEST431_2:
      primary_id = gfx::ColorSpace::PrimaryID::SMPTEST431_2;
      break;
    case PrimaryID::SMPTEST432_1:
      primary_id = gfx::ColorSpace::PrimaryID::SMPTEST432_1;
      break;
    case PrimaryID::EBU_3213_E:
      // TODO(uzair.jaleel) Need to check this once.
      primary_id = gfx::ColorSpace::PrimaryID::INVALID;
      break;
    case PrimaryID::INVALID:
    case PrimaryID::UNSPECIFIED:
      break;
  }

  switch (transfer) {
    case TransferID::BT709:
      transfer_id = gfx::ColorSpace::TransferID::BT709;
      guess |= GUESS_BT709;
      break;
    case TransferID::GAMMA22:
      transfer_id = gfx::ColorSpace::TransferID::GAMMA22;
      break;
    case TransferID::GAMMA28:
      transfer_id = gfx::ColorSpace::TransferID::GAMMA28;
      break;
    case TransferID::SMPTE170M:
      transfer_id = gfx::ColorSpace::TransferID::SMPTE170M;
      guess |= GUESS_SMPTE170M;
      break;
    case TransferID::SMPTE240M:
      transfer_id = gfx::ColorSpace::TransferID::SMPTE240M;
      guess |= GUESS_SMPTE240M;
      break;
    case TransferID::LINEAR:
      transfer_id = gfx::ColorSpace::TransferID::LINEAR;
      break;
    case TransferID::LOG:
      transfer_id = gfx::ColorSpace::TransferID::LOG;
      break;
    case TransferID::LOG_SQRT:
      transfer_id = gfx::ColorSpace::TransferID::LOG_SQRT;
      break;
    case TransferID::IEC61966_2_4:
      transfer_id = gfx::ColorSpace::TransferID::IEC61966_2_4;
      break;
    case TransferID::BT1361_ECG:
      transfer_id = gfx::ColorSpace::TransferID::BT1361_ECG;
      break;
    case TransferID::IEC61966_2_1:
      transfer_id = gfx::ColorSpace::TransferID::IEC61966_2_1;
      break;
    case TransferID::BT2020_10:
      transfer_id = gfx::ColorSpace::TransferID::BT2020_10;
      break;
    case TransferID::BT2020_12:
      transfer_id = gfx::ColorSpace::TransferID::BT2020_12;
      break;
    case TransferID::SMPTEST2084:
      transfer_id = gfx::ColorSpace::TransferID::SMPTEST2084;
      break;
    case TransferID::SMPTEST428_1:
      transfer_id = gfx::ColorSpace::TransferID::SMPTEST428_1;
      break;
    case TransferID::ARIB_STD_B67:
      transfer_id = gfx::ColorSpace::TransferID::ARIB_STD_B67;
      break;
    case TransferID::INVALID:
    case TransferID::UNSPECIFIED:
      break;
  }

  switch (matrix) {
    case MatrixID::RGB:
      // RGB-encoded video actually puts the green in the Y channel,
      // the blue in the Cb (U) channel and the red in the Cr (V) channel.
      matrix_id = gfx::ColorSpace::MatrixID::GBR;
      break;
    case MatrixID::BT709:
      matrix_id = gfx::ColorSpace::MatrixID::BT709;
      guess |= GUESS_BT709;
      break;
    case MatrixID::FCC:
      matrix_id = gfx::ColorSpace::MatrixID::FCC;
      break;
    case MatrixID::BT470BG:
      matrix_id = gfx::ColorSpace::MatrixID::BT470BG;
      guess |= GUESS_BT470BG;
      break;
    case MatrixID::SMPTE170M:
      matrix_id = gfx::ColorSpace::MatrixID::SMPTE170M;
      guess |= GUESS_SMPTE170M;
      break;
    case MatrixID::SMPTE240M:
      matrix_id = gfx::ColorSpace::MatrixID::SMPTE240M;
      guess |= GUESS_SMPTE240M;
      break;
    case MatrixID::YCOCG:
      matrix_id = gfx::ColorSpace::MatrixID::YCOCG;
      break;
    case MatrixID::BT2020_NCL:
      matrix_id = gfx::ColorSpace::MatrixID::BT2020_NCL;
      break;
    case MatrixID::BT2020_CL:
      matrix_id = gfx::ColorSpace::MatrixID::BT2020_CL;
      break;
    case MatrixID::YDZDX:
      matrix_id = gfx::ColorSpace::MatrixID::YDZDX;
      break;
    case MatrixID::INVALID:
    case MatrixID::UNSPECIFIED:
      break;
  }
  // Removes lowest bit until only a single bit remains.
  while (guess & (guess - 1)) {
    guess &= guess - 1;
  }
  if (!guess)
    guess = GUESS_BT709;

  if (primary_id == gfx::ColorSpace::PrimaryID::INVALID) {
    switch (guess) {
      case GUESS_BT709:
        primary_id = gfx::ColorSpace::PrimaryID::BT709;
        break;
      case GUESS_BT470M:
        primary_id = gfx::ColorSpace::PrimaryID::BT470M;
        break;
      case GUESS_BT470BG:
        primary_id = gfx::ColorSpace::PrimaryID::BT470BG;
        break;
      case GUESS_SMPTE170M:
        primary_id = gfx::ColorSpace::PrimaryID::SMPTE170M;
        break;
      case GUESS_SMPTE240M:
        primary_id = gfx::ColorSpace::PrimaryID::SMPTE240M;
        break;
    }
  }

  if (transfer_id == gfx::ColorSpace::TransferID::INVALID) {
    switch (guess) {
      case GUESS_BT709:
        transfer_id = gfx::ColorSpace::TransferID::BT709;
        break;
      case GUESS_BT470M:
      case GUESS_BT470BG:
      case GUESS_SMPTE170M:
        transfer_id = gfx::ColorSpace::TransferID::SMPTE170M;
        break;
      case GUESS_SMPTE240M:
        transfer_id = gfx::ColorSpace::TransferID::SMPTE240M;
        break;
    }
  }

  if (matrix_id == gfx::ColorSpace::MatrixID::INVALID) {
    switch (guess) {
      case GUESS_BT709:
        matrix_id = gfx::ColorSpace::MatrixID::BT709;
        break;
      case GUESS_BT470M:
      case GUESS_BT470BG:
      case GUESS_SMPTE170M:
        matrix_id = gfx::ColorSpace::MatrixID::SMPTE170M;
        break;
      case GUESS_SMPTE240M:
        matrix_id = gfx::ColorSpace::MatrixID::SMPTE240M;
        break;
    }
  }

  return gfx::ColorSpace(primary_id, transfer_id, matrix_id, range);
}

VideoColorSpace VideoColorSpace::REC709() {
  return VideoColorSpace(PrimaryID::BT709, TransferID::BT709, MatrixID::BT709,
                         gfx::ColorSpace::RangeID::LIMITED);
}
VideoColorSpace VideoColorSpace::REC601() {
  return VideoColorSpace(PrimaryID::SMPTE170M, TransferID::SMPTE170M,
                         MatrixID::SMPTE170M,
                         gfx::ColorSpace::RangeID::LIMITED);
}
VideoColorSpace VideoColorSpace::JPEG() {
  // TODO(ccameron): Determine which primaries and transfer function were
  // intended here.
  return VideoColorSpace(PrimaryID::BT709, TransferID::IEC61966_2_1,
                         MatrixID::SMPTE170M, gfx::ColorSpace::RangeID::FULL);
}

// static
VideoColorSpace VideoColorSpace::FromGfxColorSpace(
    const gfx::ColorSpace& color_space) {
  media::VideoColorSpace::PrimaryID primaries =
      media::VideoColorSpace::PrimaryID::INVALID;
  switch (color_space.GetPrimaryID()) {
    case gfx::ColorSpace::PrimaryID::BT709:
      primaries = media::VideoColorSpace::PrimaryID::BT709;
      break;
    case gfx::ColorSpace::PrimaryID::BT470M:
      primaries = media::VideoColorSpace::PrimaryID::BT470M;
      break;
    case gfx::ColorSpace::PrimaryID::BT470BG:
      primaries = media::VideoColorSpace::PrimaryID::BT470BG;
      break;
    case gfx::ColorSpace::PrimaryID::SMPTE170M:
      primaries = media::VideoColorSpace::PrimaryID::SMPTE170M;
      break;
    case gfx::ColorSpace::PrimaryID::SMPTE240M:
      primaries = media::VideoColorSpace::PrimaryID::SMPTE240M;
      break;
    case gfx::ColorSpace::PrimaryID::FILM:
      primaries = media::VideoColorSpace::PrimaryID::FILM;
      break;
    case gfx::ColorSpace::PrimaryID::BT2020:
      primaries = media::VideoColorSpace::PrimaryID::BT2020;
      break;
    case gfx::ColorSpace::PrimaryID::SMPTEST428_1:
      primaries = media::VideoColorSpace::PrimaryID::SMPTEST428_1;
      break;
    case gfx::ColorSpace::PrimaryID::SMPTEST431_2:
      primaries = media::VideoColorSpace::PrimaryID::SMPTEST431_2;
      break;
    case gfx::ColorSpace::PrimaryID::SMPTEST432_1:
      primaries = media::VideoColorSpace::PrimaryID::SMPTEST432_1;
      break;
    default:
      break;
  }

  media::VideoColorSpace::TransferID transfer =
      media::VideoColorSpace::TransferID::INVALID;
  switch (color_space.GetTransferID()) {
    case gfx::ColorSpace::TransferID::BT709:
      transfer = media::VideoColorSpace::TransferID::BT709;
      break;
    case gfx::ColorSpace::TransferID::GAMMA22:
      transfer = media::VideoColorSpace::TransferID::GAMMA22;
      break;
    case gfx::ColorSpace::TransferID::GAMMA28:
      transfer = media::VideoColorSpace::TransferID::GAMMA28;
      break;
    case gfx::ColorSpace::TransferID::SMPTE170M:
      transfer = media::VideoColorSpace::TransferID::SMPTE170M;
      break;
    case gfx::ColorSpace::TransferID::SMPTE240M:
      transfer = media::VideoColorSpace::TransferID::SMPTE240M;
      break;
    case gfx::ColorSpace::TransferID::LINEAR:
      transfer = media::VideoColorSpace::TransferID::LINEAR;
      break;
    case gfx::ColorSpace::TransferID::LOG:
      transfer = media::VideoColorSpace::TransferID::LOG;
      break;
    case gfx::ColorSpace::TransferID::LOG_SQRT:
      transfer = media::VideoColorSpace::TransferID::LOG_SQRT;
      break;
    case gfx::ColorSpace::TransferID::IEC61966_2_4:
      transfer = media::VideoColorSpace::TransferID::IEC61966_2_4;
      break;
    case gfx::ColorSpace::TransferID::BT1361_ECG:
      transfer = media::VideoColorSpace::TransferID::BT1361_ECG;
      break;
    case gfx::ColorSpace::TransferID::IEC61966_2_1:
      transfer = media::VideoColorSpace::TransferID::IEC61966_2_1;
      break;
    case gfx::ColorSpace::TransferID::BT2020_10:
      transfer = media::VideoColorSpace::TransferID::BT2020_10;
      break;
    case gfx::ColorSpace::TransferID::BT2020_12:
      transfer = media::VideoColorSpace::TransferID::BT2020_12;
      break;
    case gfx::ColorSpace::TransferID::SMPTEST2084:
      transfer = media::VideoColorSpace::TransferID::SMPTEST2084;
      break;
    case gfx::ColorSpace::TransferID::SMPTEST428_1:
      transfer = media::VideoColorSpace::TransferID::SMPTEST428_1;
      break;
    case gfx::ColorSpace::TransferID::ARIB_STD_B67:
      transfer = media::VideoColorSpace::TransferID::ARIB_STD_B67;
      break;
    default:
      break;
  }

  media::VideoColorSpace::MatrixID matrix =
      media::VideoColorSpace::MatrixID::INVALID;
  switch (color_space.GetMatrixID()) {
    case gfx::ColorSpace::MatrixID::RGB:
      matrix = media::VideoColorSpace::MatrixID::RGB;
      break;
    case gfx::ColorSpace::MatrixID::BT709:
      matrix = media::VideoColorSpace::MatrixID::BT709;
      break;
    case gfx::ColorSpace::MatrixID::FCC:
      matrix = media::VideoColorSpace::MatrixID::FCC;
      break;
    case gfx::ColorSpace::MatrixID::BT470BG:
      matrix = media::VideoColorSpace::MatrixID::BT470BG;
      break;
    case gfx::ColorSpace::MatrixID::SMPTE170M:
      matrix = media::VideoColorSpace::MatrixID::SMPTE170M;
      break;
    case gfx::ColorSpace::MatrixID::SMPTE240M:
      matrix = media::VideoColorSpace::MatrixID::SMPTE240M;
      break;
    case gfx::ColorSpace::MatrixID::YCOCG:
      matrix = media::VideoColorSpace::MatrixID::YCOCG;
      break;
    case gfx::ColorSpace::MatrixID::BT2020_NCL:
      matrix = media::VideoColorSpace::MatrixID::BT2020_NCL;
      break;
    case gfx::ColorSpace::MatrixID::BT2020_CL:
      matrix = media::VideoColorSpace::MatrixID::BT2020_CL;
      break;
    case gfx::ColorSpace::MatrixID::YDZDX:
      matrix = media::VideoColorSpace::MatrixID::YDZDX;
      break;
    default:
      break;
  }

  return media::VideoColorSpace(primaries, transfer, matrix,
                                color_space.GetRangeID());
}

}  // namespace
