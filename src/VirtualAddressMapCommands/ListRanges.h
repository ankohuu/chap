// Copyright (c) 2018-2019,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../SizedTally.h"
#include "../VirtualMemoryPartition.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class ListRanges : public Commands::Subcommand {
 public:
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename VirtualMemoryPartition<Offset>::ClaimedRanges ClaimedRanges;
  ListRanges(const std::string& subcommandName, const std::string& helpMessage,
             const std::string& tallyDescriptor, const ClaimedRanges& ranges)
      : Commands::Subcommand("list", subcommandName),
        _helpMessage(helpMessage),
        _tallyDescriptor(tallyDescriptor),
        _ranges(ranges) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << _helpMessage;
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, _tallyDescriptor);
    for (const auto& range : _ranges) {
      tally.AdjustTally(range._size);
      output << "Range [0x" << std::hex << range._base << ", 0x" << range._limit
             << ") uses 0x" << range._size << " bytes.\n";
    }
  }

 private:
  const std::string _helpMessage;
  const std::string _tallyDescriptor;
  const ClaimedRanges& _ranges;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
