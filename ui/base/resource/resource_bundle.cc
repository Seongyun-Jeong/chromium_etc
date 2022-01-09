// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/resource/resource_bundle.h"

#include <stdint.h>

#include <limits>
#include <utility>
#include <vector>

#include "base/big_endian.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/debug/alias.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/scoped_refptr.h"
#include "base/notreached.h"
#include "base/numerics/safe_conversions.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/trace_event/base_tracing.h"
#include "build/build_config.h"
#include "net/filter/gzip_header.h"
#include "skia/ext/image_operations.h"
#include "third_party/brotli/include/brotli/decode.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/zlib/google/compression_utils.h"
#include "ui/base/buildflags.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/layout.h"
#include "ui/base/resource/data_pack.h"
#include "ui/base/ui_base_paths.h"
#include "ui/base/ui_base_switches.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_source.h"
#include "ui/strings/grit/app_locale_settings.h"
#include "url/gurl.h"

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#include "ui/base/resource/resource_bundle_android.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)
#include "ui/gfx/platform_font_skia.h"
#endif

#if defined(OS_WIN)
#include "ui/display/win/dpi.h"

// To avoid conflicts with the macro from the Windows SDK...
#undef LoadBitmap
#endif

namespace ui {

namespace {

// PNG-related constants.
const unsigned char kPngMagic[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
const size_t kPngChunkMetadataSize = 12;  // length, type, crc32
const unsigned char kPngScaleChunkType[4] = { 'c', 's', 'C', 'l' };
const unsigned char kPngDataChunkType[4] = { 'I', 'D', 'A', 'T' };

#if !defined(OS_APPLE)
const char kPakFileExtension[] = ".pak";
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
// The prefix that GRIT prepends to Lottie assets, after compression if any.
// See: tools/grit/grit/node/structure.py
constexpr char kLottiePrefix[6] = {'L', 'O', 'T', 'T', 'I', 'E'};

// Points to |lottie::ParseLottieAsStillImage| so that certain dependencies do
// not need to be included directly in ui/base.
ResourceBundle::LottieImageParseFunction g_parse_lottie_as_still_image_ =
    nullptr;
#endif

ResourceBundle* g_shared_instance_ = nullptr;

base::FilePath GetResourcesPakFilePath(const std::string& pak_name) {
  base::FilePath path;
  if (base::PathService::Get(base::DIR_ASSETS, &path))
    return path.AppendASCII(pak_name.c_str());

  // Return just the name of the pak file.
#if defined(OS_WIN)
  return base::FilePath(base::ASCIIToWide(pak_name));
#else
  return base::FilePath(pak_name.c_str());
#endif  // OS_WIN
}

SkBitmap CreateEmptyBitmap() {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(32, 32);
  bitmap.eraseARGB(255, 255, 255, 0);
  return bitmap;
}

// Helper function for determining whether a resource is gzipped.
bool HasGzipHeader(base::StringPiece data) {
  net::GZipHeader header;
  const char* header_end = nullptr;
  net::GZipHeader::Status header_status =
      header.ReadMore(data.data(), data.length(), &header_end);
  return header_status == net::GZipHeader::COMPLETE_HEADER;
}

// Helper function for determining whether a resource is brotli compressed.
bool HasBrotliHeader(base::StringPiece data) {
  // Check that the data is brotli decoded by checking for kBrotliConst in
  // header. Header added during compression at tools/grit/grit/node/base.py.
  const uint8_t* data_bytes = reinterpret_cast<const uint8_t*>(data.data());
  static_assert(base::size(ResourceBundle::kBrotliConst) == 2,
                "Magic number should be 2 bytes long");
  return data.size() >= ResourceBundle::kBrotliHeaderSize &&
         *data_bytes == ResourceBundle::kBrotliConst[0] &&
         *(data_bytes + 1) == ResourceBundle::kBrotliConst[1];
}

// Returns the uncompressed size of Brotli compressed |input| from header.
size_t GetBrotliDecompressSize(base::StringPiece input) {
  CHECK(input.data());
  CHECK(HasBrotliHeader(input));
  const uint8_t* raw_input = reinterpret_cast<const uint8_t*>(input.data());
  raw_input = raw_input + base::size(ResourceBundle::kBrotliConst);
  // Get size of uncompressed resource from header.
  uint64_t uncompress_size = 0;
  int bytes_size = static_cast<int>(ResourceBundle::kBrotliHeaderSize -
                                    base::size(ResourceBundle::kBrotliConst));
  for (int i = 0; i < bytes_size; i++) {
    uncompress_size |= static_cast<uint64_t>(*(raw_input + i)) << (i * 8);
  }
  return static_cast<size_t>(uncompress_size);
}

// Decompresses data in |input| using brotli, storing
// the result in |output|, which is resized as necessary. Returns true for
// success. To be used for grit compressed resources only.
bool BrotliDecompress(base::StringPiece input, std::string* output) {
  size_t decompress_size = GetBrotliDecompressSize(input);
  const uint8_t* raw_input = reinterpret_cast<const uint8_t*>(input.data());
  raw_input = raw_input + ResourceBundle::kBrotliHeaderSize;

  output->resize(decompress_size);
  uint8_t* output_bytes =
      reinterpret_cast<uint8_t*>(const_cast<char*>(output->data()));
  return BrotliDecoderDecompress(
             input.size() - ResourceBundle::kBrotliHeaderSize, raw_input,
             &decompress_size, output_bytes) == BROTLI_DECODER_RESULT_SUCCESS;
}

// Helper function for decompressing resource.
void DecompressIfNeeded(base::StringPiece data, std::string* output) {
  if (!data.empty() && HasGzipHeader(data)) {
    TRACE_EVENT0("ui", "DecompressIfNeeded::GzipUncompress");
    bool success = compression::GzipUncompress(data, output);
    DCHECK(success);
  } else if (!data.empty() && HasBrotliHeader(data)) {
    TRACE_EVENT0("ui", "DecompressIfNeeded::BrotliDecompress");
    bool success = BrotliDecompress(data, output);
    DCHECK(success);
  } else {
    // Assume the raw data is not compressed.
    output->assign(data.data(), data.size());
  }
}

}  // namespace

// A descendant of |gfx::ImageSkiaSource| that loads a bitmap image for the
// requested scale factor from |ResourceBundle| on demand for a given
// |resource_id|. If the bitmap for the requested scale factor does not exist,
// it will return the 1x bitmap scaled by the scale factor. This may lead to
// broken UI if the correct size of the scaled image is not exactly
// |scale_factor| * the size of the 1x bitmap. When
// --highlight-missing-scaled-resources flag is specified, scaled 1x bitmaps are
// highlighted by blending them with red.
class ResourceBundle::BitmapImageSource : public gfx::ImageSkiaSource {
 public:
  BitmapImageSource(ResourceBundle* rb, int resource_id)
      : rb_(rb), resource_id_(resource_id) {}

  BitmapImageSource(const BitmapImageSource&) = delete;
  BitmapImageSource& operator=(const BitmapImageSource&) = delete;
  ~BitmapImageSource() override = default;

  // gfx::ImageSkiaSource overrides:
  gfx::ImageSkiaRep GetImageForScale(float scale) override {
    SkBitmap image;
    bool fell_back_to_1x = false;
    ResourceScaleFactor scale_factor = GetSupportedResourceScaleFactor(scale);
    bool found = rb_->LoadBitmap(resource_id_, &scale_factor,
                                 &image, &fell_back_to_1x);
    if (!found) {
#if defined(OS_ANDROID)
      // TODO(oshima): Android unit_tests runs at DSF=3 with 100P assets.
      return gfx::ImageSkiaRep();
#else
      NOTREACHED() << "Unable to load bitmap image with id " << resource_id_
                   << ", scale=" << scale;
      return gfx::ImageSkiaRep(CreateEmptyBitmap(), scale);
#endif
    }

    // If the resource is in the package with kScaleFactorNone, it
    // can be used in any scale factor. The image is marked as "unscaled"
    // so that the ImageSkia do not automatically scale.
    if (scale_factor == ui::kScaleFactorNone)
      return gfx::ImageSkiaRep(image, 0.0f);

    if (fell_back_to_1x) {
      // GRIT fell back to the 100% image, so rescale it to the correct size.
      image = skia::ImageOperations::Resize(
          image, skia::ImageOperations::RESIZE_LANCZOS3,
          base::ClampCeil(image.width() * scale),
          base::ClampCeil(image.height() * scale));
    } else {
      scale = GetScaleForResourceScaleFactor(scale_factor);
    }
    return gfx::ImageSkiaRep(image, scale);
  }

 private:
  raw_ptr<ResourceBundle> rb_;

  const int resource_id_;
};

#if BUILDFLAG(IS_CHROMEOS_ASH)
// A descendant of |gfx::ImageSkiaSource| that simply uses one
// |gfx::ImageSkiaRep| for all scales.
class ResourceBundle::LottieImageSource : public gfx::ImageSkiaSource {
 public:
  LottieImageSource(const gfx::ImageSkiaRep& rep) : rep_(rep) {}
  LottieImageSource(const LottieImageSource&) = delete;
  LottieImageSource& operator=(const LottieImageSource&) = delete;
  ~LottieImageSource() override = default;

  // gfx::ImageSkiaSource overrides:
  gfx::ImageSkiaRep GetImageForScale(float scale) override { return rep_; }
  bool HasRepresentationAtAllScales() const override { return true; }

 private:
  gfx::ImageSkiaRep rep_;
};
#endif

ResourceBundle::FontDetails::FontDetails(std::string typeface,
                                         int size_delta,
                                         gfx::Font::Weight weight)
    : typeface(typeface), size_delta(size_delta), weight(weight) {}

bool ResourceBundle::FontDetails::operator==(const FontDetails& rhs) const {
  return std::tie(typeface, size_delta, weight) ==
         std::tie(rhs.typeface, rhs.size_delta, rhs.weight);
}

bool ResourceBundle::FontDetails::operator<(const FontDetails& rhs) const {
  return std::tie(typeface, size_delta, weight) <
         std::tie(rhs.typeface, rhs.size_delta, rhs.weight);
}

// static
std::string ResourceBundle::InitSharedInstanceWithLocale(
    const std::string& pref_locale,
    Delegate* delegate,
    LoadResources load_resources) {
  InitSharedInstance(delegate);
  if (load_resources == LOAD_COMMON_RESOURCES)
    g_shared_instance_->LoadCommonResources();
  std::string result =
      g_shared_instance_->LoadLocaleResources(pref_locale,
                                              /*crash_on_failure=*/true);
  g_shared_instance_->InitDefaultFontList();
  return result;
}

// static
void ResourceBundle::InitSharedInstanceWithPakFileRegion(
    base::File pak_file,
    const base::MemoryMappedFile::Region& region) {
  InitSharedInstance(nullptr);
  auto data_pack = std::make_unique<DataPack>(k100Percent);
  if (!data_pack->LoadFromFileRegion(std::move(pak_file), region)) {
    LOG(WARNING) << "failed to load pak file";
    NOTREACHED();
    return;
  }
  g_shared_instance_->locale_resources_data_ = std::move(data_pack);
  g_shared_instance_->InitDefaultFontList();
}

// static
void ResourceBundle::InitSharedInstanceWithPakPath(const base::FilePath& path) {
  InitSharedInstance(nullptr);
  g_shared_instance_->LoadTestResources(path, path);

  g_shared_instance_->InitDefaultFontList();
}

// static
void ResourceBundle::CleanupSharedInstance() {
  delete g_shared_instance_;
  g_shared_instance_ = nullptr;
}

// static
ResourceBundle* ResourceBundle::SwapSharedInstanceForTesting(
    ResourceBundle* instance) {
  ResourceBundle* ret = g_shared_instance_;
  g_shared_instance_ = instance;
  return ret;
}

// static
bool ResourceBundle::HasSharedInstance() {
  return g_shared_instance_ != nullptr;
}

// static
ResourceBundle& ResourceBundle::GetSharedInstance() {
  // Must call InitSharedInstance before this function.
  CHECK(g_shared_instance_ != nullptr);
  return *g_shared_instance_;
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
// static
void ResourceBundle::SetParseLottieAsStillImage(
    ResourceBundle::LottieImageParseFunction parse_lottie_as_still_image) {
  g_parse_lottie_as_still_image_ = parse_lottie_as_still_image;
}
#endif

void ResourceBundle::LoadSecondaryLocaleDataWithPakFileRegion(
    base::File pak_file,
    const base::MemoryMappedFile::Region& region) {
  auto data_pack = std::make_unique<DataPack>(k100Percent);
  if (!data_pack->LoadFromFileRegion(std::move(pak_file), region)) {
    LOG(WARNING) << "failed to load secondary pak file";
    NOTREACHED();
    return;
  }
  secondary_locale_resources_data_ = std::move(data_pack);
}

#if !defined(OS_ANDROID)
// static
bool ResourceBundle::LocaleDataPakExists(const std::string& locale) {
  const auto path = GetLocaleFilePath(locale);
  return !path.empty() && base::PathExists(path);
}
#endif  // !defined(OS_ANDROID)

void ResourceBundle::AddDataPackFromPath(const base::FilePath& path,
                                         ResourceScaleFactor scale_factor) {
  AddDataPackFromPathInternal(path, scale_factor, false);
}

void ResourceBundle::AddOptionalDataPackFromPath(
    const base::FilePath& path,
    ResourceScaleFactor scale_factor) {
  AddDataPackFromPathInternal(path, scale_factor, true);
}

void ResourceBundle::AddDataPackFromBuffer(base::span<const uint8_t> buffer,
                                           ResourceScaleFactor scale_factor) {
  std::unique_ptr<DataPack> data_pack(new DataPack(scale_factor));
  if (data_pack->LoadFromBuffer(buffer)) {
    AddDataPack(std::move(data_pack));
  } else {
    LOG(ERROR) << "Failed to load data pack from buffer";
  }
}

void ResourceBundle::AddDataPackFromFileRegion(
    base::File file,
    const base::MemoryMappedFile::Region& region,
    ResourceScaleFactor scale_factor) {
  auto data_pack = std::make_unique<DataPack>(scale_factor);
  if (data_pack->LoadFromFileRegion(std::move(file), region)) {
    AddDataPack(std::move(data_pack));
  } else {
    LOG(ERROR) << "Failed to load data pack from file."
               << "\nSome features may not be available.";
  }
}

#if !defined(OS_APPLE)
// static
base::FilePath ResourceBundle::GetLocaleFilePath(
    const std::string& app_locale) {
  if (app_locale.empty())
    return base::FilePath();

  base::FilePath locale_file_path;
  if (base::PathService::Get(ui::DIR_LOCALES, &locale_file_path)) {
    locale_file_path =
        locale_file_path.AppendASCII(app_locale + kPakFileExtension);
  }

  // Note: The delegate GetPathForLocalePack() override is currently only used
  // by CastResourceDelegate, which does not call this function prior to
  // initializing the ResourceBundle. This called earlier than that by the
  // variations code which also has a CHECK that an inconsistent value does not
  // get returned via VariationsService::EnsureLocaleEquals().
  if (HasSharedInstance() && GetSharedInstance().delegate_) {
    locale_file_path = GetSharedInstance().delegate_->GetPathForLocalePack(
        locale_file_path, app_locale);
  }

  // Don't try to load from paths that are not absolute.
  return locale_file_path.IsAbsolute() ? locale_file_path : base::FilePath();
}
#endif

#if !defined(OS_ANDROID)
std::string ResourceBundle::LoadLocaleResources(const std::string& pref_locale,
                                                bool crash_on_failure) {
  DCHECK(!locale_resources_data_.get()) << "locale.pak already loaded";
  std::string app_locale = l10n_util::GetApplicationLocale(pref_locale);
  base::FilePath locale_file_path = GetOverriddenPakPath();
  if (locale_file_path.empty())
    locale_file_path = GetLocaleFilePath(app_locale);

  if (locale_file_path.empty()) {
    // It's possible that there is no locale.pak.
    LOG(WARNING) << "locale_file_path.empty() for locale " << app_locale;
    return std::string();
  }

  auto data_pack = std::make_unique<DataPack>(k100Percent);
  if (!data_pack->LoadFromPath(locale_file_path) && crash_on_failure) {
    // https://crbug.com/1076423: Chrome can't start when the locale file cannot
    // be loaded. Crash early and gather some data.
#if defined(OS_WIN)
    const auto last_error = ::GetLastError();
    base::debug::Alias(&last_error);
    wchar_t path_copy[MAX_PATH];
    base::wcslcpy(path_copy, locale_file_path.value().c_str(),
                  base::size(path_copy));
    base::debug::Alias(path_copy);
#endif  // defined(OS_WIN)
    CHECK(false);
  }

  locale_resources_data_ = std::move(data_pack);
  loaded_locale_ = pref_locale;
  return app_locale;
}
#endif  // defined(OS_ANDROID)

void ResourceBundle::LoadTestResources(const base::FilePath& path,
                                       const base::FilePath& locale_path) {
  is_test_resources_ = true;
  DCHECK(!ui::GetSupportedResourceScaleFactors().empty());
  // Use the given resource pak for both common and localized resources.

  if (!path.empty()) {
    const ResourceScaleFactor scale_factor(
        ui::GetSupportedResourceScaleFactors()[0]);
    auto data_pack = std::make_unique<DataPack>(scale_factor);
    CHECK(data_pack->LoadFromPath(path));
    AddDataPack(std::move(data_pack));
  }

  auto data_pack = std::make_unique<DataPack>(ui::kScaleFactorNone);
  if (!locale_path.empty() && data_pack->LoadFromPath(locale_path)) {
    locale_resources_data_ = std::move(data_pack);
  } else {
    locale_resources_data_ = std::make_unique<DataPack>(ui::kScaleFactorNone);
  }

  // This is necessary to initialize ICU since we won't be calling
  // LoadLocaleResources in this case.
  l10n_util::GetApplicationLocale(std::string());
}

void ResourceBundle::UnloadLocaleResources() {
  locale_resources_data_.reset();
  secondary_locale_resources_data_.reset();
}

void ResourceBundle::OverrideLocalePakForTest(const base::FilePath& pak_path) {
  overridden_pak_path_ = pak_path;
}

void ResourceBundle::OverrideLocaleStringResource(
    int resource_id,
    const std::u16string& string) {
  overridden_locale_strings_[resource_id] = string;
}

const base::FilePath& ResourceBundle::GetOverriddenPakPath() const {
  return overridden_pak_path_;
}

std::u16string ResourceBundle::MaybeMangleLocalizedString(
    const std::u16string& str) const {
  if (!mangle_localized_strings_)
    return str;

  // IDS_MINIMUM_FONT_SIZE and friends are localization "strings" that are
  // actually integral constants. These should not be mangled or they become
  // impossible to parse.
  int ignored;
  if (base::StringToInt(str, &ignored))
    return str;

  // IDS_WEBSTORE_URL and some other resources are localization "strings" that
  // are actually URLs, where the "localized" part is actually just the language
  // code embedded in the URL. Don't mangle any URL.
  if (GURL(str).is_valid())
    return str;

  // For a string S, produce [[ --- S --- ]], where the number of dashes is 1/4
  // of the number of characters in S. This makes S something around 50-75%
  // longer, except for extremely short strings, which get > 100% longer.
  std::u16string start_marker = u"[[";
  std::u16string end_marker = u"]]";
  std::u16string dashes = std::u16string(str.size() / 4, '-');
  return base::JoinString({start_marker, dashes, str, dashes, end_marker},
                          u" ");
}

std::string ResourceBundle::ReloadLocaleResources(
    const std::string& pref_locale) {
  base::AutoLock lock_scope(*locale_resources_data_lock_);

  // Remove all overriden strings, as they will not be valid for the new locale.
  overridden_locale_strings_.clear();

  UnloadLocaleResources();
  return LoadLocaleResources(pref_locale, /*crash_on_failure=*/false);
}

gfx::ImageSkia* ResourceBundle::GetImageSkiaNamed(int resource_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const gfx::ImageSkia* image = GetImageNamed(resource_id).ToImageSkia();
  return const_cast<gfx::ImageSkia*>(image);
}

gfx::Image& ResourceBundle::GetImageNamed(int resource_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Check to see if the image is already in the cache.
  auto found = images_.find(resource_id);
  if (found != images_.end())
    return found->second;

  gfx::Image image;
  if (delegate_)
    image = delegate_->GetImageNamed(resource_id);

  if (image.IsEmpty()) {
    gfx::ImageSkia image_skia = CreateImageSkia(resource_id);
    if (image_skia.isNull()) {
      LOG(WARNING) << "Unable to load image with id " << resource_id;
      NOTREACHED();  // Want to assert in debug mode.
      // The load failed to retrieve the image; show a debugging red square.
      return GetEmptyImage();
    }
    image_skia.SetReadOnly();
    image = gfx::Image(image_skia);
  }

  // The load was successful, so cache the image.
  auto inserted = images_.emplace(resource_id, image);
  DCHECK(inserted.second);
  return inserted.first->second;
}

constexpr uint8_t ResourceBundle::kBrotliConst[];

base::RefCountedMemory* ResourceBundle::LoadDataResourceBytes(
    int resource_id) const {
  return LoadDataResourceBytesForScale(resource_id, ui::kScaleFactorNone);
}

base::RefCountedMemory* ResourceBundle::LoadDataResourceBytesForScale(
    int resource_id,
    ResourceScaleFactor scale_factor) const {
  TRACE_EVENT("ui", "ResourceBundle::LoadDataResourceBytesForScale",
              [&](perfetto::EventContext ctx) {
                auto* event =
                    ctx.event<perfetto::protos::pbzero::ChromeTrackEvent>();
                auto* data = event->set_resource_bundle();
                data->set_resource_id(resource_id);
              });

  if (delegate_) {
    base::RefCountedMemory* bytes =
        delegate_->LoadDataResourceBytes(resource_id, scale_factor);
    if (bytes)
      return bytes;
  }

  base::StringPiece data =
      GetRawDataResourceForScale(resource_id, scale_factor);
  if (data.empty())
    return nullptr;

  if (HasGzipHeader(data) || HasBrotliHeader(data)) {
    base::RefCountedString* bytes_string = new base::RefCountedString();
    DecompressIfNeeded(data, &(bytes_string->data()));
    return bytes_string;
  }

  return new base::RefCountedStaticMemory(data.data(), data.length());
}

base::StringPiece ResourceBundle::GetRawDataResource(int resource_id) const {
  return GetRawDataResourceForScale(resource_id, ui::kScaleFactorNone);
}

base::StringPiece ResourceBundle::GetRawDataResourceForScale(
    int resource_id,
    ResourceScaleFactor scale_factor,
    ResourceScaleFactor* loaded_scale_factor) const {
  base::StringPiece data;
  if (delegate_ &&
      delegate_->GetRawDataResource(resource_id, scale_factor, &data)) {
    if (loaded_scale_factor)
      *loaded_scale_factor = scale_factor;
    return data;
  }

  if (scale_factor != ui::k100Percent) {
    for (size_t i = 0; i < data_packs_.size(); i++) {
      if (data_packs_[i]->GetResourceScaleFactor() == scale_factor &&
          data_packs_[i]->GetStringPiece(static_cast<uint16_t>(resource_id),
                                         &data)) {
        if (loaded_scale_factor)
          *loaded_scale_factor = scale_factor;
        return data;
      }
    }
  }

  for (size_t i = 0; i < data_packs_.size(); i++) {
    if ((data_packs_[i]->GetResourceScaleFactor() == ui::k100Percent ||
         data_packs_[i]->GetResourceScaleFactor() == ui::k200Percent ||
         data_packs_[i]->GetResourceScaleFactor() == ui::k300Percent ||
         data_packs_[i]->GetResourceScaleFactor() == ui::kScaleFactorNone) &&
        data_packs_[i]->GetStringPiece(static_cast<uint16_t>(resource_id),
                                       &data)) {
      if (loaded_scale_factor)
        *loaded_scale_factor = data_packs_[i]->GetResourceScaleFactor();
      return data;
    }
  }
  if (loaded_scale_factor)
    *loaded_scale_factor = ui::kScaleFactorNone;
  return base::StringPiece();
}

std::string ResourceBundle::LoadDataResourceString(int resource_id) const {
  if (delegate_) {
    absl::optional<std::string> data =
        delegate_->LoadDataResourceString(resource_id);
    if (data)
      return data.value();
  }

  return LoadDataResourceStringForScale(resource_id, ui::kScaleFactorNone);
}

std::string ResourceBundle::LoadDataResourceStringForScale(
    int resource_id,
    ResourceScaleFactor scaling_factor) const {
  std::string output;
  DecompressIfNeeded(GetRawDataResourceForScale(resource_id, scaling_factor),
                     &output);
  return output;
}

std::string ResourceBundle::LoadLocalizedResourceString(int resource_id) const {
  base::AutoLock lock_scope(*locale_resources_data_lock_);
  base::StringPiece data;
  if (!(locale_resources_data_.get() &&
        locale_resources_data_->GetStringPiece(
            static_cast<uint16_t>(resource_id), &data) &&
        !data.empty())) {
    if (secondary_locale_resources_data_.get() &&
        secondary_locale_resources_data_->GetStringPiece(
            static_cast<uint16_t>(resource_id), &data) &&
        !data.empty()) {
    } else {
      data = GetRawDataResource(resource_id);
    }
  }
  std::string output;
  DecompressIfNeeded(data, &output);
  return output;
}

bool ResourceBundle::IsGzipped(int resource_id) const {
  base::StringPiece raw_data = GetRawDataResource(resource_id);
  if (!raw_data.data())
    return false;

  return HasGzipHeader(raw_data);
}

bool ResourceBundle::IsBrotli(int resource_id) const {
  base::StringPiece raw_data = GetRawDataResource(resource_id);
  if (!raw_data.data())
    return false;

  return HasBrotliHeader(raw_data);
}

std::u16string ResourceBundle::GetLocalizedString(int resource_id) {
#if DCHECK_IS_ON()
  {
    base::AutoLock lock_scope(*locale_resources_data_lock_);
    // Overriding locale strings isn't supported if the first string resource
    // has already been queried.
    can_override_locale_string_resources_ = false;
  }
#endif
  return GetLocalizedStringImpl(resource_id);
}

base::RefCountedMemory* ResourceBundle::LoadLocalizedResourceBytes(
    int resource_id) const {
  {
    base::AutoLock lock_scope(*locale_resources_data_lock_);
    base::StringPiece data;

    if (locale_resources_data_.get() &&
        locale_resources_data_->GetStringPiece(
            static_cast<uint16_t>(resource_id), &data) &&
        !data.empty()) {
      return new base::RefCountedStaticMemory(data.data(), data.length());
    }

    if (secondary_locale_resources_data_.get() &&
        secondary_locale_resources_data_->GetStringPiece(
            static_cast<uint16_t>(resource_id), &data) &&
        !data.empty()) {
      return new base::RefCountedStaticMemory(data.data(), data.length());
    }
  }
  // Release lock_scope and fall back to main data pack.
  return LoadDataResourceBytes(resource_id);
}

const gfx::FontList& ResourceBundle::GetFontListWithDelta(int size_delta) {
  return GetFontListForDetails(FontDetails(std::string(), size_delta));
}

const gfx::FontList& ResourceBundle::GetFontListForDetails(
    const FontDetails& details) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto found = font_cache_.find(details);
  if (found != font_cache_.end())
    return found->second;

  const FontDetails base_details(details.typeface);
  gfx::FontList default_font_list = gfx::FontList();
  gfx::FontList base_font_list =
      details.typeface.empty()
          ? default_font_list
          : gfx::FontList({details.typeface}, default_font_list.GetFontStyle(),
                          default_font_list.GetFontSize(),
                          default_font_list.GetFontWeight());
  font_cache_.emplace(base_details, base_font_list);
  gfx::FontList& base = font_cache_.find(base_details)->second;
  if (details == base_details)
    return base;

  // Fonts of a given style are derived from the unstyled font of the same size.
  // Cache the unstyled font by first inserting a default-constructed font list.
  // Then, derive it for the initial insertion, or use the iterator that points
  // to the existing entry that the insertion collided with.
  const FontDetails sized_details(details.typeface, details.size_delta);
  auto sized = font_cache_.emplace(sized_details, base_font_list);
  if (sized.second)
    sized.first->second = base.DeriveWithSizeDelta(details.size_delta);
  if (details == sized_details) {
    return sized.first->second;
  }

  auto styled = font_cache_.emplace(details, base_font_list);
  DCHECK(styled.second);  // Otherwise font_cache_.find(..) would have found it.
  styled.first->second = sized.first->second.Derive(
      0, sized.first->second.GetFontStyle(), details.weight);

  return styled.first->second;
}

const gfx::FontList& ResourceBundle::GetFontList(FontStyle legacy_style) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  gfx::Font::Weight weight = gfx::Font::Weight::NORMAL;
  if (legacy_style == BoldFont || legacy_style == MediumBoldFont)
    weight = gfx::Font::Weight::BOLD;

  int size_delta = 0;
  switch (legacy_style) {
    case SmallFont:
      size_delta = kSmallFontDelta;
      break;
    case MediumFont:
    case MediumBoldFont:
      size_delta = kMediumFontDelta;
      break;
    case LargeFont:
      size_delta = kLargeFontDelta;
      break;
    case BaseFont:
    case BoldFont:
      break;
  }

  return GetFontListForDetails(FontDetails(std::string(), size_delta, weight));
}

const gfx::Font& ResourceBundle::GetFont(FontStyle style) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return GetFontList(style).GetPrimaryFont();
}

void ResourceBundle::ReloadFonts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  InitDefaultFontList();
  font_cache_.clear();
}

ResourceScaleFactor ResourceBundle::GetMaxResourceScaleFactor() const {
#if defined(OS_WIN) || defined(OS_LINUX) || defined(OS_CHROMEOS)
  return max_scale_factor_;
#else
  return GetSupportedResourceScaleFactors().back();
#endif
}

bool ResourceBundle::IsScaleFactorSupported(ResourceScaleFactor scale_factor) {
  const std::vector<ResourceScaleFactor>& supported_scale_factors =
      ui::GetSupportedResourceScaleFactors();
  return base::Contains(supported_scale_factors, scale_factor);
}

void ResourceBundle::CheckCanOverrideStringResources() {
#if DCHECK_IS_ON()
  base::AutoLock lock_scope(*locale_resources_data_lock_);
  DCHECK(can_override_locale_string_resources_);
#endif
}

ResourceBundle::ResourceBundle(Delegate* delegate)
    : delegate_(delegate),
      locale_resources_data_lock_(new base::Lock),
      max_scale_factor_(k100Percent) {
  mangle_localized_strings_ = base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kMangleLocalizedStrings);
}

ResourceBundle::~ResourceBundle() {
  FreeImages();
  UnloadLocaleResources();
}

// static
void ResourceBundle::InitSharedInstance(Delegate* delegate) {
  DCHECK(g_shared_instance_ == nullptr) << "ResourceBundle initialized twice";
  g_shared_instance_ = new ResourceBundle(delegate);
  std::vector<ResourceScaleFactor> supported_scale_factors;
#if defined(OS_IOS)
  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  if (display.device_scale_factor() > 2.0) {
    DCHECK_EQ(3.0, display.device_scale_factor());
    supported_scale_factors.push_back(k300Percent);
  } else if (display.device_scale_factor() > 1.0) {
    DCHECK_EQ(2.0, display.device_scale_factor());
    supported_scale_factors.push_back(k200Percent);
  } else {
    supported_scale_factors.push_back(k100Percent);
  }
#else
  // On platforms other than iOS, 100P is always a supported scale factor.
  supported_scale_factors.push_back(k100Percent);

#if BUILDFLAG(ENABLE_HIDPI)
  supported_scale_factors.push_back(k200Percent);
#endif
#endif
  ui::SetSupportedResourceScaleFactors(supported_scale_factors);
}

void ResourceBundle::FreeImages() {
  images_.clear();
}

void ResourceBundle::LoadChromeResources() {
  // Always load the 1x data pack first as the 2x data pack contains both 1x and
  // 2x images. The 1x data pack only has 1x images, thus passes in an accurate
  // scale factor to gfx::ImageSkia::AddRepresentation.
  if (IsScaleFactorSupported(k100Percent)) {
    AddDataPackFromPath(GetResourcesPakFilePath("chrome_100_percent.pak"),
                        k100Percent);
  }

  if (IsScaleFactorSupported(k200Percent)) {
    AddOptionalDataPackFromPath(
        GetResourcesPakFilePath("chrome_200_percent.pak"), k200Percent);
  }
}

void ResourceBundle::AddDataPackFromPathInternal(
    const base::FilePath& path,
    ResourceScaleFactor scale_factor,
    bool optional) {
  // Do not pass an empty |path| value to this method. If the absolute path is
  // unknown pass just the pack file name.
  DCHECK(!path.empty());

  base::FilePath pack_path = path;
  if (delegate_)
    pack_path = delegate_->GetPathForResourcePack(pack_path, scale_factor);

  // Don't try to load empty values or values that are not absolute paths.
  if (pack_path.empty() || !pack_path.IsAbsolute())
    return;

  auto data_pack = std::make_unique<DataPack>(scale_factor);
  if (data_pack->LoadFromPath(pack_path)) {
    AddDataPack(std::move(data_pack));
  } else if (!optional) {
    LOG(ERROR) << "Failed to load " << pack_path.value()
               << "\nSome features may not be available.";
  }
}

void ResourceBundle::AddDataPack(std::unique_ptr<DataPack> data_pack) {
#if DCHECK_IS_ON()
  data_pack->CheckForDuplicateResources(data_packs_);
#endif

  if (GetScaleForResourceScaleFactor(data_pack->GetResourceScaleFactor()) >
      GetScaleForResourceScaleFactor(max_scale_factor_))
    max_scale_factor_ = data_pack->GetResourceScaleFactor();

  data_packs_.push_back(std::move(data_pack));
}

void ResourceBundle::InitDefaultFontList() {
#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)
  // InitDefaultFontList() is called earlier than overriding the locale strings.
  // So we call the |GetLocalizedStringImpl()| which doesn't set the flag
  // |can_override_locale_string_resources_| to false. This is okay, because the
  // font list doesn't need to be overridden by variations.
  std::string font_family =
      base::UTF16ToUTF8(GetLocalizedStringImpl(IDS_UI_FONT_FAMILY_CROS));
  gfx::FontList::SetDefaultFontDescription(font_family);

  // TODO(yukishiino): Remove SetDefaultFontDescription() once the migration to
  // the font list is done.  We will no longer need SetDefaultFontDescription()
  // after every client gets started using a FontList instead of a Font.
  gfx::PlatformFontSkia::SetDefaultFontDescription(font_family);
#else
  // Use a single default font as the default font list.
  gfx::FontList::SetDefaultFontDescription(std::string());
#endif
}

gfx::ImageSkia ResourceBundle::CreateImageSkia(int resource_id) {
  DCHECK(!data_packs_.empty()) << "Missing call to SetResourcesDataDLL?";
#if BUILDFLAG(IS_CHROMEOS_ASH)
  gfx::ImageSkiaRep rep_from_lottie;
  if (LoadLottie(resource_id, &rep_from_lottie)) {
    return gfx::ImageSkia(std::make_unique<LottieImageSource>(rep_from_lottie),
                          rep_from_lottie.pixel_size());
  }
  const ResourceScaleFactor scale_factor_to_load = GetMaxResourceScaleFactor();
#elif defined(OS_WIN)
  const ResourceScaleFactor scale_factor_to_load =
      display::win::GetDPIScale() > 1.25 ? GetMaxResourceScaleFactor()
                                         : ui::k100Percent;
#else
  const ResourceScaleFactor scale_factor_to_load = ui::k100Percent;
#endif
  // TODO(oshima): Consider reading the image size from png IHDR chunk and
  // skip decoding here and remove #ifdef below.
  // |ResourceBundle::GetSharedInstance()| is destroyed after the
  // |BrowserMainLoop| has finished running. The |gfx::ImageSkia| is guaranteed
  // to be destroyed before the resource bundle is destroyed.
  return gfx::ImageSkia(std::make_unique<BitmapImageSource>(this, resource_id),
                        GetScaleForResourceScaleFactor(scale_factor_to_load));
}

bool ResourceBundle::LoadBitmap(const ResourceHandle& data_handle,
                                int resource_id,
                                SkBitmap* bitmap,
                                bool* fell_back_to_1x) const {
  DCHECK(fell_back_to_1x);
  scoped_refptr<base::RefCountedMemory> memory(
      data_handle.GetStaticMemory(static_cast<uint16_t>(resource_id)));
  if (!memory.get())
    return false;

  if (DecodePNG(memory->front(), memory->size(), bitmap, fell_back_to_1x))
    return true;

#if !defined(OS_IOS)
  // iOS does not compile or use the JPEG codec.  On other platforms,
  // 99% of our assets are PNGs, however fallback to JPEG.
  std::unique_ptr<SkBitmap> jpeg_bitmap(
      gfx::JPEGCodec::Decode(memory->front(), memory->size()));
  if (jpeg_bitmap.get()) {
    bitmap->swap(*jpeg_bitmap.get());
    *fell_back_to_1x = false;
    return true;
  }
#endif

  NOTREACHED() << "Unable to decode theme image resource " << resource_id;
  return false;
}

bool ResourceBundle::LoadBitmap(int resource_id,
                                ResourceScaleFactor* scale_factor,
                                SkBitmap* bitmap,
                                bool* fell_back_to_1x) const {
  DCHECK(fell_back_to_1x);
  for (const auto& pack : data_packs_) {
    if (pack->GetResourceScaleFactor() == ui::kScaleFactorNone &&
        LoadBitmap(*pack, resource_id, bitmap, fell_back_to_1x)) {
      DCHECK(!*fell_back_to_1x);
      *scale_factor = ui::kScaleFactorNone;
      return true;
    }

    if (pack->GetResourceScaleFactor() == *scale_factor &&
        LoadBitmap(*pack, resource_id, bitmap, fell_back_to_1x)) {
      return true;
    }
  }

  // Unit tests may only have 1x data pack. Allow them to fallback to 1x
  // resources.
  if (is_test_resources_ && *scale_factor != ui::k100Percent) {
    for (const auto& pack : data_packs_) {
      if (pack->GetResourceScaleFactor() == ui::k100Percent &&
          LoadBitmap(*pack, resource_id, bitmap, fell_back_to_1x)) {
        *fell_back_to_1x = true;
        return true;
      }
    }
  }

  return false;
}

gfx::Image& ResourceBundle::GetEmptyImage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (empty_image_.IsEmpty()) {
    // The placeholder bitmap is bright red so people notice the problem.
    SkBitmap bitmap = CreateEmptyBitmap();
    empty_image_ = gfx::Image::CreateFrom1xBitmap(bitmap);
  }
  return empty_image_;
}

std::u16string ResourceBundle::GetLocalizedStringImpl(int resource_id) const {
  std::u16string string;
  if (delegate_ && delegate_->GetLocalizedString(resource_id, &string))
    return MaybeMangleLocalizedString(string);

  // Ensure that ReloadLocaleResources() doesn't drop the resources while
  // we're using them.
  base::AutoLock lock_scope(*locale_resources_data_lock_);

  IdToStringMap::const_iterator it =
      overridden_locale_strings_.find(resource_id);
  if (it != overridden_locale_strings_.end())
    return MaybeMangleLocalizedString(it->second);

  // If for some reason we were unable to load the resources , return an empty
  // string (better than crashing).
  if (!locale_resources_data_.get()) {
    LOG(WARNING) << "locale resources are not loaded";
    return std::u16string();
  }

  base::StringPiece data;
  ResourceHandle::TextEncodingType encoding =
      locale_resources_data_->GetTextEncodingType();
  if (!locale_resources_data_->GetStringPiece(
          static_cast<uint16_t>(resource_id), &data)) {
    if (secondary_locale_resources_data_.get() &&
        secondary_locale_resources_data_->GetStringPiece(
            static_cast<uint16_t>(resource_id), &data)) {
      // Fall back on the secondary locale pak if it exists.
      encoding = secondary_locale_resources_data_->GetTextEncodingType();
    } else {
      // Fall back on the main data pack (shouldn't be any strings here except
      // in unittests).
      data = GetRawDataResource(resource_id);
      CHECK(!data.empty())
          << "Unable to find resource: " << resource_id
          << ". If this happens in a browser test running on Windows, it may "
             "be that dead-code elimination stripped out the code that uses the"
             " resource, causing the resource to be stripped out because the "
             "resource is not used by chrome.dll. See "
             "https://crbug.com/1181150.";
    }
  }

  // Strings should not be loaded from a data pack that contains binary data.
  DCHECK(encoding == ResourceHandle::UTF16 || encoding == ResourceHandle::UTF8)
      << "requested localized string from binary pack file";

  // Data pack encodes strings as either UTF8 or UTF16.
  std::u16string msg;
  if (encoding == ResourceHandle::UTF16) {
    msg = std::u16string(reinterpret_cast<const char16_t*>(data.data()),
                         data.length() / 2);
  } else if (encoding == ResourceHandle::UTF8) {
    msg = base::UTF8ToUTF16(data);
  }
  return MaybeMangleLocalizedString(msg);
}

// static
bool ResourceBundle::PNGContainsFallbackMarker(const unsigned char* buf,
                                               size_t size) {
  if (size < base::size(kPngMagic) ||
      memcmp(buf, kPngMagic, base::size(kPngMagic)) != 0) {
    // Data invalid or a JPEG.
    return false;
  }
  size_t pos = base::size(kPngMagic);

  // Scan for custom chunks until we find one, find the IDAT chunk, or run out
  // of chunks.
  for (;;) {
    if (size - pos < kPngChunkMetadataSize)
      break;
    uint32_t length = 0;
    base::ReadBigEndian(buf + pos, &length);
    if (size - pos - kPngChunkMetadataSize < length)
      break;
    if (length == 0 && memcmp(buf + pos + sizeof(uint32_t), kPngScaleChunkType,
                              base::size(kPngScaleChunkType)) == 0) {
      return true;
    }
    if (memcmp(buf + pos + sizeof(uint32_t), kPngDataChunkType,
               base::size(kPngDataChunkType)) == 0) {
      // Stop looking for custom chunks, any custom chunks should be before an
      // IDAT chunk.
      break;
    }
    pos += length + kPngChunkMetadataSize;
  }
  return false;
}

// static
bool ResourceBundle::DecodePNG(const unsigned char* buf,
                               size_t size,
                               SkBitmap* bitmap,
                               bool* fell_back_to_1x) {
  *fell_back_to_1x = PNGContainsFallbackMarker(buf, size);
  return gfx::PNGCodec::Decode(buf, size, bitmap);
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
bool ResourceBundle::LoadLottie(int resource_id, gfx::ImageSkiaRep* rep) const {
  const base::StringPiece potential_lottie = GetRawDataResource(resource_id);
  if (potential_lottie.substr(0u, base::size(kLottiePrefix)) !=
      base::StringPiece(kLottiePrefix, base::size(kLottiePrefix)))
    return false;

  auto bytes_string = base::MakeRefCounted<base::RefCountedString>();
  DecompressIfNeeded(potential_lottie.substr(base::size(kLottiePrefix)),
                     &(bytes_string->data()));
  *rep = (*g_parse_lottie_as_still_image_)(*bytes_string);
  return true;
}
#endif

}  // namespace ui
