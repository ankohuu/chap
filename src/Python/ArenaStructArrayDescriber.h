// Copyright (c) 2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace Python {
template <typename Offset>
class ArenaStructArrayDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  ArenaStructArrayDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage,
                                              "PythonArenaStructArray"),
        _infrastructureFinder(processImage.GetPythonInfrastructureFinder()),
        _contiguousImage(processImage.GetVirtualAddressMap(),
                         processImage.GetAllocationDirectory()) {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& /* allocation */,
                        bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern PythonArenaStructArray.\n";
    output << "There are " << std::dec
           << _infrastructureFinder.ArenaStructCount() << " entries of size 0x"
           << std::hex << _infrastructureFinder.ArenaStructSize()
           << " in the array.\n";
    output << std::dec << _infrastructureFinder.NumArenas()
           << " entries in the array"
           << " have corresponding python arenas.\n";
    _contiguousImage.SetIndex(index);
    // TODO: Possibly dump the array as part of the description.
    if (explain) {
    }
  }

 private:
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
};
}  // namespace Python
}  // namespace chap
