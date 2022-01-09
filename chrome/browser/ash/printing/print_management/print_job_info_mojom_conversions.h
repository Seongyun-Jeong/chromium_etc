// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_PRINTING_PRINT_MANAGEMENT_PRINT_JOB_INFO_MOJOM_CONVERSIONS_H_
#define CHROME_BROWSER_ASH_PRINTING_PRINT_MANAGEMENT_PRINT_JOB_INFO_MOJOM_CONVERSIONS_H_

#include "ash/webui/print_management/mojom/printing_manager.mojom.h"

namespace chromeos {
class CupsPrintJob;
namespace printing {
namespace proto {
class PrintJobInfo;
}  // namespace proto
}  // namespace printing
}  // namespace chromeos

namespace ash {
namespace printing {
namespace print_management {

// Converts proto::PrintJobInfo into mojom::PrintJobInfoPtr.
printing_manager::mojom::PrintJobInfoPtr PrintJobProtoToMojom(
    const chromeos::printing::proto::PrintJobInfo& print_job_info_proto);

// Convert CupsPrintJob into mojom::PrintJobInfoPtr.
printing_manager::mojom::PrintJobInfoPtr CupsPrintJobToMojom(
    const chromeos::CupsPrintJob& job);

}  // namespace print_management
}  // namespace printing
}  // namespace ash

#endif  // CHROME_BROWSER_ASH_PRINTING_PRINT_MANAGEMENT_PRINT_JOB_INFO_MOJOM_CONVERSIONS_H_
