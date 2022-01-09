// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/data_decoder/data_decoder_service.h"

#include <memory>

#include "base/bind.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/web_package/web_bundle_parser_factory.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "services/data_decoder/gzipper.h"
#include "services/data_decoder/json_parser_impl.h"
#include "services/data_decoder/public/mojom/image_decoder.mojom.h"
#include "services/data_decoder/web_bundler.h"
#include "services/data_decoder/xml_parser.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "services/data_decoder/ble_scan_parser_impl.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if !defined(OS_IOS)
#include "services/data_decoder/image_decoder_impl.h"
#endif

namespace data_decoder {

DataDecoderService::DataDecoderService() = default;

DataDecoderService::DataDecoderService(
    mojo::PendingReceiver<mojom::DataDecoderService> receiver) {
  receivers_.Add(this, std::move(receiver));
}

DataDecoderService::~DataDecoderService() = default;

void DataDecoderService::BindReceiver(
    mojo::PendingReceiver<mojom::DataDecoderService> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void DataDecoderService::BindImageDecoder(
    mojo::PendingReceiver<mojom::ImageDecoder> receiver) {
#if defined(OS_IOS)
  LOG(FATAL) << "ImageDecoder not supported on iOS.";
#else
  if (drop_image_decoders_)
    return;
  mojo::MakeSelfOwnedReceiver(std::make_unique<ImageDecoderImpl>(),
                              std::move(receiver));
#endif
}

void DataDecoderService::BindJsonParser(
    mojo::PendingReceiver<mojom::JsonParser> receiver) {
  if (drop_json_parsers_)
    return;
  mojo::MakeSelfOwnedReceiver(std::make_unique<JsonParserImpl>(),
                              std::move(receiver));
}

void DataDecoderService::BindXmlParser(
    mojo::PendingReceiver<mojom::XmlParser> receiver) {
  mojo::MakeSelfOwnedReceiver(std::make_unique<XmlParser>(),
                              std::move(receiver));
}

void DataDecoderService::BindWebBundleParserFactory(
    mojo::PendingReceiver<web_package::mojom::WebBundleParserFactory>
        receiver) {
  if (web_bundle_parser_factory_binder_) {
    web_bundle_parser_factory_binder_.Run(std::move(receiver));
  } else {
    mojo::MakeSelfOwnedReceiver(
        std::make_unique<web_package::WebBundleParserFactory>(),
        std::move(receiver));
  }
}

void DataDecoderService::BindWebBundler(
    mojo::PendingReceiver<mojom::WebBundler> receiver) {
  if (web_bundler_binder_) {
    web_bundler_binder_.Run(std::move(receiver));
  } else {
    mojo::MakeSelfOwnedReceiver(std::make_unique<WebBundler>(),
                                std::move(receiver));
  }
}

void DataDecoderService::BindGzipper(
    mojo::PendingReceiver<mojom::Gzipper> receiver) {
  mojo::MakeSelfOwnedReceiver(std::make_unique<Gzipper>(), std::move(receiver));
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
void DataDecoderService::BindBleScanParser(
    mojo::PendingReceiver<mojom::BleScanParser> receiver) {
  mojo::MakeSelfOwnedReceiver(std::make_unique<BleScanParserImpl>(),
                              std::move(receiver));
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

}  // namespace data_decoder
