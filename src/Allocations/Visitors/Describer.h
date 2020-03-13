// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../../SizedTally.h"
#include "../Describer.h"
#include "../Directory.h"
namespace chap {
namespace Allocations {
namespace Visitors {
template <class Offset>
class Describer {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  class Factory {
   public:
    Factory(const chap::Allocations::Describer<Offset>& describer)
        : _describer(describer), _commandName("describe") {}
    Describer* MakeVisitor(Commands::Context& context,
                           const ProcessImage<Offset>& /* processImage */) {
      return new Describer(context, _describer);
    }
    const std::string& GetCommandName() const { return _commandName; }
    // TODO: allow adding taints
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "In this case \"describe\" means show the address, size,"
                "anchored/leaked/free\n"
                "status and type if known.\n";
    }

   private:
    const Allocations::Describer<Offset>& _describer;
    const std::string _commandName;
    const std::vector<std::string> _taints;
  };

  Describer(Commands::Context& context,
            const Allocations::Describer<Offset>& describer)
      : _context(context),
        _describer(describer),
        _sizedTally(context, "allocations") {}
  void Visit(AllocationIndex index, const Allocation& allocation) {
    size_t size = allocation.Size();
    _sizedTally.AdjustTally(size);
    _describer.Describe(_context, index, allocation, false, 0, false);
  }

 private:
  Commands::Context& _context;
  const Allocations::Describer<Offset>& _describer;
  SizedTally<Offset> _sizedTally;
};
}  // namespace Visitors
}  // namespace Allocations
}  // namespace chap
