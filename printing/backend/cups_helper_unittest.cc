// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/backend/cups_helper.h"

#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "printing/backend/print_backend.h"
#include "printing/mojom/print.mojom.h"
#include "printing/print_settings.h"
#include "printing/printing_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace printing {

namespace {

// Returns true if the papers have the same name, vendor ID, and size.
bool PapersEqual(const PrinterSemanticCapsAndDefaults::Paper& lhs,
                 const PrinterSemanticCapsAndDefaults::Paper& rhs) {
  return lhs.display_name == rhs.display_name &&
         lhs.vendor_id == rhs.vendor_id && lhs.size_um == rhs.size_um;
}

void VerifyCapabilityColorModels(const PrinterSemanticCapsAndDefaults& caps) {
  absl::optional<bool> maybe_color = IsColorModelSelected(caps.color_model);
  ASSERT_TRUE(maybe_color.has_value());
  EXPECT_TRUE(maybe_color.value());
  maybe_color = IsColorModelSelected(caps.bw_model);
  ASSERT_TRUE(maybe_color.has_value());
  EXPECT_FALSE(maybe_color.value());
}

std::string GeneratePpdResolutionTestData(const char* res_name) {
  return base::StringPrintf(R"(*PPD-Adobe: 4.3
*OpenUI *%1$s/%1$s: PickOne
*%1$s 600dpi/600 dpi: " "
*Default%1$s: 600dpi
*CloseUI: *%1$s)",
                            res_name);
}

}  // namespace

TEST(PrintBackendCupsHelperTest, TestPpdParsingNoColorDuplexShortEdge) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenGroup: General/General
*OpenUI *ColorModel/Color Model: PickOne
*DefaultColorModel: Gray
*ColorModel Gray/Grayscale: "
  <</cupsColorSpace 0/cupsColorOrder 0>>setpagedevice"
*ColorModel Black/Inverted Grayscale: "
  <</cupsColorSpace 3/cupsColorOrder 0>>setpagedevice"
*CloseUI: *ColorModel
*OpenUI *Duplex/2-Sided Printing: PickOne
*DefaultDuplex: DuplexTumble
*Duplex None/Off: "
  <</Duplex false>>setpagedevice"
*Duplex DuplexNoTumble/LongEdge: "
  </Duplex true/Tumble false>>setpagedevice"
*Duplex DuplexTumble/ShortEdge: "
  <</Duplex true/Tumble true>>setpagedevice"
*CloseUI: *Duplex
*CloseGroup: General)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.collate_capable);
  EXPECT_TRUE(caps.collate_default);
  EXPECT_EQ(caps.copies_max, 9999);
  EXPECT_THAT(caps.duplex_modes,
              testing::UnorderedElementsAre(mojom::DuplexMode::kSimplex,
                                            mojom::DuplexMode::kLongEdge,
                                            mojom::DuplexMode::kShortEdge));
  EXPECT_EQ(mojom::DuplexMode::kShortEdge, caps.duplex_default);
  EXPECT_FALSE(caps.color_changeable);
  EXPECT_FALSE(caps.color_default);
}

// Test duplex detection code, which regressed in http://crbug.com/103999.
TEST(PrintBackendCupsHelperTest, TestPpdParsingNoColorDuplexSimples) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenGroup: General/General
*OpenUI *Duplex/Double-Sided Printing: PickOne
*DefaultDuplex: None
*Duplex None/Off: "
  <</Duplex false>>setpagedevice"
*Duplex DuplexNoTumble/Long Edge (Standard): "
  <</Duplex true/Tumble false>>setpagedevice"
*Duplex DuplexTumble/Short Edge (Flip): "
  <</Duplex true/Tumble true>>setpagedevice"
*CloseUI: *Duplex
*CloseGroup: General)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.collate_capable);
  EXPECT_TRUE(caps.collate_default);
  EXPECT_EQ(caps.copies_max, 9999);
  EXPECT_THAT(caps.duplex_modes,
              testing::UnorderedElementsAre(mojom::DuplexMode::kSimplex,
                                            mojom::DuplexMode::kLongEdge,
                                            mojom::DuplexMode::kShortEdge));
  EXPECT_EQ(mojom::DuplexMode::kSimplex, caps.duplex_default);
  EXPECT_FALSE(caps.color_changeable);
  EXPECT_FALSE(caps.color_default);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingNoColorNoDuplex) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenGroup: General/General
*OpenUI *ColorModel/Color Model: PickOne
*DefaultColorModel: Gray
*ColorModel Gray/Grayscale: "
  <</cupsColorSpace 0/cupsColorOrder 0>>setpagedevice"
*ColorModel Black/Inverted Grayscale: "
  <</cupsColorSpace 3/cupsColorOrder 0>>setpagedevice"
*CloseUI: *ColorModel
*CloseGroup: General)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.collate_capable);
  EXPECT_TRUE(caps.collate_default);
  EXPECT_EQ(caps.copies_max, 9999);
  EXPECT_THAT(caps.duplex_modes, testing::UnorderedElementsAre());
  EXPECT_EQ(mojom::DuplexMode::kUnknownDuplexMode, caps.duplex_default);
  EXPECT_FALSE(caps.color_changeable);
  EXPECT_FALSE(caps.color_default);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingColorTrueDuplexShortEdge) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*DefaultColorSpace: CMYK
*OpenGroup: General/General
*OpenUI *ColorModel/Color Model: PickOne
*DefaultColorModel: CMYK
*ColorModel CMYK/Color: "(cmyk) RCsetdevicecolor"
*ColorModel Gray/Black and White: "(gray) RCsetdevicecolor"
*CloseUI: *ColorModel
*OpenUI *Duplex/2-Sided Printing: PickOne
*DefaultDuplex: DuplexTumble
*Duplex None/Off: "
  <</Duplex false>>setpagedevice"
*Duplex DuplexNoTumble/LongEdge: "
  <</Duplex true/Tumble false>>setpagedevice"
*Duplex DuplexTumble/ShortEdge: "
  <</Duplex true/Tumble true>>setpagedevice"
*CloseUI: *Duplex
*CloseGroup: General)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.collate_capable);
  EXPECT_TRUE(caps.collate_default);
  EXPECT_EQ(caps.copies_max, 9999);
  EXPECT_THAT(caps.duplex_modes,
              testing::UnorderedElementsAre(mojom::DuplexMode::kSimplex,
                                            mojom::DuplexMode::kLongEdge,
                                            mojom::DuplexMode::kShortEdge));
  EXPECT_EQ(mojom::DuplexMode::kShortEdge, caps.duplex_default);
  EXPECT_TRUE(caps.color_changeable);
  EXPECT_TRUE(caps.color_default);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingColorFalseDuplexLongEdge) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*DefaultColorSpace: CMYK
*OpenGroup: General/General
*OpenUI *ColorModel/Color Model: PickOne
*DefaultColorModel: Grayscale
*ColorModel Color/Color: "
  %% FoomaticRIPOptionSetting: ColorModel=Color"
*FoomaticRIPOptionSetting ColorModel=Color: "
  JCLDatamode=Color GSCmdLine=Color"
*ColorModel Grayscale/Grayscale: "
  %% FoomaticRIPOptionSetting: ColorModel=Grayscale"
*FoomaticRIPOptionSetting ColorModel=Grayscale: "
  JCLDatamode=Grayscale GSCmdLine=Grayscale"
*CloseUI: *ColorModel
*OpenUI *Duplex/2-Sided Printing: PickOne
*DefaultDuplex: DuplexNoTumble
*Duplex None/Off: "
  <</Duplex false>>setpagedevice"
*Duplex DuplexNoTumble/LongEdge: "
  <</Duplex true/Tumble false>>setpagedevice"
*Duplex DuplexTumble/ShortEdge: "
  <</Duplex true/Tumble true>>setpagedevice"
*CloseUI: *Duplex
*CloseGroup: General)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.collate_capable);
  EXPECT_TRUE(caps.collate_default);
  EXPECT_EQ(caps.copies_max, 9999);
  EXPECT_THAT(caps.duplex_modes,
              testing::UnorderedElementsAre(mojom::DuplexMode::kSimplex,
                                            mojom::DuplexMode::kLongEdge,
                                            mojom::DuplexMode::kShortEdge));
  EXPECT_EQ(mojom::DuplexMode::kLongEdge, caps.duplex_default);
  EXPECT_TRUE(caps.color_changeable);
  EXPECT_FALSE(caps.color_default);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingPageSize) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenUI *PageSize: PickOne
*DefaultPageSize: Legal
*PageSize Letter/US Letter: "
  <</DeferredMediaSelection true /PageSize [612 792]
  /ImagingBBox null /MediaClass null >> setpagedevice"
*End
*PageSize Legal/US Legal: "
  <</DeferredMediaSelection true /PageSize [612 1008]
  /ImagingBBox null /MediaClass null >> setpagedevice"
*End
*DefaultPaperDimension: Legal
*PaperDimension Letter/US Letter: "612   792"
*PaperDimension Legal/US Legal: "612  1008"
*CloseUI: *PageSize)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  ASSERT_EQ(2UL, caps.papers.size());
  EXPECT_EQ("Letter", caps.papers[0].vendor_id);
  EXPECT_EQ("US Letter", caps.papers[0].display_name);
  EXPECT_EQ(215900, caps.papers[0].size_um.width());
  EXPECT_EQ(279400, caps.papers[0].size_um.height());
  EXPECT_EQ("Legal", caps.papers[1].vendor_id);
  EXPECT_EQ("US Legal", caps.papers[1].display_name);
  EXPECT_EQ(215900, caps.papers[1].size_um.width());
  EXPECT_EQ(355600, caps.papers[1].size_um.height());
  EXPECT_TRUE(PapersEqual(caps.papers[1], caps.default_paper));
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingPageSizeNoDefaultSpecified) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenUI *PageSize: PickOne
*PageSize A3/ISO A3: "
  << /DeferredMediaSelection true /PageSize [842 1191]
  /ImagingBBox null >> setpagedevice"
*End
*PageSize A4/ISO A4: "
  << /DeferredMediaSelection true /PageSize [595 842]
  /ImagingBBox null >> setpagedevice"
*End
*PageSize Legal/US Legal: "
  << /DeferredMediaSelection true /PageSize [612 1008]
  /ImagingBBox null >> setpagedevice"
*End
*PageSize Letter/US Letter: "
  << /DeferredMediaSelection true /PageSize [612 792]
  /ImagingBBox null >> setpagedevice"
*End
*PaperDimension A3/ISO A3: "842 1191"
*PaperDimension A4/ISO A4: "595 842"
*PaperDimension Legal/US Legal: "612 1008"
*PaperDimension Letter/US Letter: "612 792"
*CloseUI: *PageSize)";

  {
    PrinterSemanticCapsAndDefaults caps;
    EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"en-US",
                                     kTestPpdData, &caps));
    ASSERT_EQ(4UL, caps.papers.size());
    EXPECT_EQ("Letter", caps.papers[3].vendor_id);
    EXPECT_EQ("US Letter", caps.papers[3].display_name);
    EXPECT_EQ(215900, caps.papers[3].size_um.width());
    EXPECT_EQ(279400, caps.papers[3].size_um.height());
    EXPECT_TRUE(PapersEqual(caps.papers[3], caps.default_paper));
  }
  {
    PrinterSemanticCapsAndDefaults caps;
    EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"en-UK",
                                     kTestPpdData, &caps));
    ASSERT_EQ(4UL, caps.papers.size());
    EXPECT_EQ("A4", caps.papers[1].vendor_id);
    EXPECT_EQ("ISO A4", caps.papers[1].display_name);
    EXPECT_EQ(209903, caps.papers[1].size_um.width());
    EXPECT_EQ(297039, caps.papers[1].size_um.height());
    EXPECT_TRUE(PapersEqual(caps.papers[1], caps.default_paper));
  }
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingBrotherPrinters) {
  {
    constexpr char kTestPpdData[] =
        R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*OpenUI *BRPrintQuality/Color/Mono: PickOne
*DefaultBRPrintQuality: Auto
*BRPrintQuality Auto/Auto: ""
*BRPrintQuality Color/Color: ""
*BRPrintQuality Black/Mono: ""
*CloseUI: *BRPrintQuality)";

    PrinterSemanticCapsAndDefaults caps;
    EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                     kTestPpdData, &caps));
    EXPECT_TRUE(caps.color_changeable);
    EXPECT_TRUE(caps.color_default);
    EXPECT_EQ(mojom::ColorModel::kBrotherBRScript3Color, caps.color_model);
    EXPECT_EQ(mojom::ColorModel::kBrotherBRScript3Black, caps.bw_model);
    VerifyCapabilityColorModels(caps);
  }
  {
    constexpr char kTestPpdData[] =
        R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*OpenUI *BRMonoColor/Color / Mono: PickOne
*DefaultBRMonoColor: Auto
*BRMonoColor Auto/Auto: ""
*BRMonoColor FullColor/Color: ""
*BRMonoColor Mono/Mono: ""
*CloseUI: *BRMonoColor)";

    PrinterSemanticCapsAndDefaults caps;
    EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                     kTestPpdData, &caps));
    EXPECT_TRUE(caps.color_changeable);
    EXPECT_TRUE(caps.color_default);
    EXPECT_EQ(mojom::ColorModel::kBrotherCUPSColor, caps.color_model);
    EXPECT_EQ(mojom::ColorModel::kBrotherCUPSMono, caps.bw_model);
    VerifyCapabilityColorModels(caps);
  }
  {
    constexpr char kTestPpdData[] =
        R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*OpenUI *BRDuplex/Two-Sided Printing: PickOne
*DefaultBRDuplex: DuplexTumble
*BRDuplex DuplexTumble/Short-Edge Binding: ""
*BRDuplex DuplexNoTumble/Long-Edge Binding: ""
*BRDuplex None/Off: ""
*CloseUI: *BRDuplex)";

    PrinterSemanticCapsAndDefaults caps;
    EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                     kTestPpdData, &caps));
    EXPECT_THAT(caps.duplex_modes,
                testing::UnorderedElementsAre(mojom::DuplexMode::kSimplex,
                                              mojom::DuplexMode::kLongEdge,
                                              mojom::DuplexMode::kShortEdge));
    EXPECT_EQ(mojom::DuplexMode::kShortEdge, caps.duplex_default);
  }
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingHpPrinters) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*OpenUI *HPColorMode/Mode: PickOne
*DefaultHPColorMode: ColorPrint
*HPColorMode ColorPrint/Color: "
  << /ProcessColorModel /DeviceCMYK >> setpagedevice"
*HPColorMode GrayscalePrint/Grayscale: "
  << /ProcessColorModel /DeviceGray >> setpagedevice"
*CloseUI: *HPColorMode)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.color_changeable);
  EXPECT_TRUE(caps.color_default);
  EXPECT_EQ(mojom::ColorModel::kHPColorColor, caps.color_model);
  EXPECT_EQ(mojom::ColorModel::kHPColorBlack, caps.bw_model);
  VerifyCapabilityColorModels(caps);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingEpsonPrinters) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*OpenUI *Ink/Ink: PickOne
*DefaultInk: COLOR
*Ink COLOR/Color: "
  <</cupsBitsPerColor 8 /cupsColorOrder 0
  /cupsColorSpace 1 /cupsCompression 1>> setpagedevice"
*Ink MONO/Monochrome: "
  <</cupsBitsPerColor 8 /cupsColorOrder 0
  /cupsColorSpace 0 /cupsCompression 1>> setpagedevice"
*CloseUI: *Ink)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.color_changeable);
  EXPECT_TRUE(caps.color_default);
  EXPECT_EQ(mojom::ColorModel::kEpsonInkColor, caps.color_model);
  EXPECT_EQ(mojom::ColorModel::kEpsonInkMono, caps.bw_model);
  VerifyCapabilityColorModels(caps);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingSamsungPrinters) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*OpenUI *ColorMode/Color Mode:  Boolean
*DefaultColorMode: True
*ColorMode False/Grayscale: ""
*ColorMode True/Color: ""
*CloseUI: *ColorMode)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.color_changeable);
  EXPECT_TRUE(caps.color_default);
  EXPECT_EQ(mojom::ColorModel::kColorModeColor, caps.color_model);
  EXPECT_EQ(mojom::ColorModel::kColorModeMonochrome, caps.bw_model);
  VerifyCapabilityColorModels(caps);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingSharpPrinters) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*OpenUI *ARCMode/Color Mode: PickOne
*OrderDependency: 180 AnySetup *ARCMode
*DefaultARCMode: CMAuto
*ARCMode CMAuto/Automatic: ""
*End
*ARCMode CMColor/Color: ""
*End
*ARCMode CMBW/Black and White: ""
*End
*CloseUI: *ARCMode)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.color_changeable);
  EXPECT_TRUE(caps.color_default);
  EXPECT_EQ(mojom::ColorModel::kSharpARCModeCMColor, caps.color_model);
  EXPECT_EQ(mojom::ColorModel::kSharpARCModeCMBW, caps.bw_model);
  VerifyCapabilityColorModels(caps);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingXeroxPrinters) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*ColorDevice: True
*OpenUI *XRXColor/Color Correction: PickOne
*OrderDependency: 48.0 AnySetup *XRXColor
*DefaultXRXColor: Automatic
*XRXColor Automatic/Automatic: "
  <</ProcessColorModel /DeviceCMYK>> setpagedevice"
*XRXColor BW/Black and White:  "
  <</ProcessColorModel /DeviceGray>> setpagedevice"
*CloseUI: *XRXColor)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.color_changeable);
  EXPECT_TRUE(caps.color_default);
  EXPECT_EQ(mojom::ColorModel::kXeroxXRXColorAutomatic, caps.color_model);
  EXPECT_EQ(mojom::ColorModel::kXeroxXRXColorBW, caps.bw_model);
  VerifyCapabilityColorModels(caps);
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingCupsMaxCopies) {
  {
    constexpr char kTestPpdData[] =
        R"(*PPD-Adobe: "4.3"
*cupsMaxCopies: 99
*OpenUI *ColorMode/Color Mode:  Boolean
*DefaultColorMode: True
*CloseUI: *ColorMode)";

    PrinterSemanticCapsAndDefaults caps;
    EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                     kTestPpdData, &caps));
    EXPECT_EQ(99, caps.copies_max);
  }

  {
    constexpr char kTestPpdData[] =
        R"(*PPD-Adobe: "4.3"
*cupsMaxCopies: notavalidnumber
*OpenUI *ColorMode/Color Mode:  Boolean
*DefaultColorMode: True
*CloseUI: *ColorMode)";

    PrinterSemanticCapsAndDefaults caps;
    EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                     kTestPpdData, &caps));
    EXPECT_EQ(9999, caps.copies_max);
  }
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingResolutionTagNames) {
  constexpr const char* kTestResNames[] = {"Resolution",     "JCLResolution",
                                           "SetResolution",  "CNRes_PGP",
                                           "HPPrintQuality", "LXResolution"};
  const std::vector<gfx::Size> kExpectedResolutions = {gfx::Size(600, 600)};
  PrinterSemanticCapsAndDefaults caps;
  for (const char* res_name : kTestResNames) {
    EXPECT_TRUE(ParsePpdCapabilities(
        /*dest=*/nullptr, /*locale=*/"",
        GeneratePpdResolutionTestData(res_name).c_str(), &caps));
    EXPECT_EQ(kExpectedResolutions, caps.dpis);
    EXPECT_EQ(kExpectedResolutions[0], caps.default_dpi);
  }
}

TEST(PrintBackendCupsHelperTest,
     TestPpdParsingResolutionInvalidDefaultResolution) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenUI *Resolution/Resolution: PickOne
*DefaultResolution: 500dpi
*Resolution 600dpi/600 dpi: ""
*CloseUI: *Resolution)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_EQ(std::vector<gfx::Size>{gfx::Size(600, 600)}, caps.dpis);
  EXPECT_TRUE(caps.default_dpi.IsEmpty());
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingResolutionNoResolution) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenUI *Resolution/Resolution: PickOne
*CloseUI: *Resolution)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_TRUE(caps.dpis.empty());
  EXPECT_TRUE(caps.default_dpi.IsEmpty());
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   "*PPD-Adobe: \"4.3\"", &caps));
  EXPECT_TRUE(caps.dpis.empty());
  EXPECT_TRUE(caps.default_dpi.IsEmpty());
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingResolutionNoDefaultResolution) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenUI *Resolution/Resolution: PickOne
*Resolution 600dpi/600 dpi: ""
*CloseUI: *Resolution)";

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_EQ(std::vector<gfx::Size>{gfx::Size(600, 600)}, caps.dpis);
  EXPECT_TRUE(caps.default_dpi.IsEmpty());
}

TEST(PrintBackendCupsHelperTest, TestPpdParsingResolutionDpiFormat) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*JCLOpenUI *Resolution/Resolution: PickOne
*OrderDependency: 100 JCLSetup *Resolution
*DefaultResolution: 600dpi
*Resolution 500x500dpi/500 dpi: " "
*Resolution 0.5dpi/0.5 dpi: " "
*Resolution 5.0dpi/5 dpi: " "
*Resolution 600dpi/600 dpi: " "
*Resolution 0dpi/0 dpi: " "
*Resolution 1e1dpi/10 dpi: " "
*Resolution -3dpi/-3 dpi: " "
*Resolution -3x300dpi/dpi: " "
*Resolution 300x0dpi/dpi: " "
*Resolution 50/50: " "
*Resolution 50dpis/50 dpis: " "
*Resolution 30x30dpis/30 dpis: " "
*Resolution 2400x600dpi/HQ1200: " "
*JCLCloseUI: *Resolution)";

  const std::vector<gfx::Size> kExpectedResolutions = {
      gfx::Size(500, 500), gfx::Size(600, 600), gfx::Size(2400, 600)};
  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(/*dest=*/nullptr, /*locale=*/"",
                                   kTestPpdData, &caps));
  EXPECT_EQ(kExpectedResolutions, caps.dpis);
  EXPECT_EQ(kExpectedResolutions[1], caps.default_dpi);
}

TEST(PrintBackendCupsHelperTest, TestPpdSetsDestOptions) {
  constexpr char kTestPpdData[] =
      R"(*PPD-Adobe: "4.3"
*OpenUI *Duplex/2-Sided Printing: PickOne
*DefaultDuplex: DuplexTumble
*Duplex None/Off: "
  <</Duplex false>>setpagedevice"
*Duplex DuplexNoTumble/LongEdge: "
  </Duplex true/Tumble false>>setpagedevice"
*Duplex DuplexTumble/ShortEdge: "
  <</Duplex true/Tumble true>>setpagedevice"
*CloseUI: *Duplex)";

  cups_dest_t* dest;
  int num_dests = 0;
  num_dests =
      cupsAddDest(/*name=*/"test_dest", /*instance=*/nullptr, num_dests, &dest);
  ASSERT_EQ(1, num_dests);

  // Set long edge duplex mode in the destination options even though the PPD
  // sets short edge duplex mode as the default.
  cups_option_t* options = nullptr;
  int num_options = 0;
  num_options =
      cupsAddOption("Duplex", "DuplexNoTumble", num_options, &options);
  dest->num_options = num_options;
  dest->options = options;

  PrinterSemanticCapsAndDefaults caps;
  EXPECT_TRUE(ParsePpdCapabilities(dest, /*locale=*/"", kTestPpdData, &caps));
  EXPECT_THAT(caps.duplex_modes,
              testing::UnorderedElementsAre(mojom::DuplexMode::kSimplex,
                                            mojom::DuplexMode::kLongEdge,
                                            mojom::DuplexMode::kShortEdge));
  EXPECT_EQ(mojom::DuplexMode::kLongEdge, caps.duplex_default);

  cupsFreeDests(num_dests, dest);
}

}  // namespace printing
