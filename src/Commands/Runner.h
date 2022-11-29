// Copyright (c) 2017-2020,2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <stack>
#include <string>
#include <vector>
#include "LineInfo.h"

#include <replxx.h>

namespace chap {
namespace Commands {

struct CommandInterruptedException {};

typedef std::vector<LineInfo> ScriptContext;
typedef std::vector<std::string> Tokens;

class Input {
 public:
  Input(ScriptContext& scriptContext) : _scriptContext(scriptContext) {
    _inputStack.push(&std::cin);
  }
  ~Input() {}
  bool StartScript(const std::string& inputPath) {
    std::ifstream* input = new std::ifstream();

    input->open(inputPath.c_str());
    if (input->fail()) {
      delete input;
      std::cerr << "Failed to open script \"" << inputPath << "\".\n";
      char* openFailCause = strerror(errno);
      if (openFailCause) {
        std::cerr << openFailCause << "\n";
      }
      return false;
    }
    _inputStack.push(input);
    _scriptContext.push_back(LineInfo(inputPath, 0));
    return true;
  }
  void TerminateAllScripts() {
    while (_inputStack.size() > 1) {
      delete _inputStack.top();
      _inputStack.pop();
    }
    _scriptContext.clear();
  }
  bool ReadLine(std::istream& is, std::string& out) {
    if (IsInScript()) {
      return !std::getline(is, out, '\n').fail();
    }

    // ANSI_COLOR_GREEN
    static char const* prompt = "\x1b[1;32mchap\x1b[0m> ";

    char* line = replxx_input(prompt);

    if (line == nullptr) {
      return false;
    }

    replxx_history_add(line);

    out = line;
    replxx_free(line);
    return true;
  }

  void GetTokens(Tokens& tokens) {
    tokens.clear();
    while (!_inputStack.empty()) {
      std::istream& input = *(_inputStack.top());
      std::string cmdLine;
      while (ReadLine(input, cmdLine)) {
        bool checkNextLine = false;
        if (cmdLine[cmdLine.size() - 1] == '\\') {
          // not quite correct if we support \ style escaping
          // there is a related issue with ' and " which must
          // be processed together with escaping.
          cmdLine.erase(cmdLine.size() - 1);
          checkNextLine = true;
        }
        if (!_scriptContext.empty()) {
          _scriptContext.back()._line++;
        }
        std::string::size_type commentPos = cmdLine.find('#');
        if (commentPos != std::string::npos) {
          cmdLine.erase(commentPos);
        }
        std::string::size_type pos = cmdLine.find_first_not_of(" \t", 0);
        while ((pos != std::string::npos) && (cmdLine[pos] == '\xc2') &&
               ((pos + 1) < cmdLine.size()) && (cmdLine[pos + 1] == '\xa0')) {
          pos = cmdLine.find_first_not_of(" \t", pos + 2);
        }
        if (pos == std::string::npos) {
          // There is no non-white space on the current line.
          if (tokens.empty() || checkNextLine) {
            // No tokens were found on a previous line or the current line
            // had a trailing '\'.  The statement may not have ended yet.
            continue;
          } else {
            // Tokens were found on some earlier line.
            // There must have been a trailing
            // '\' before a blank line.  Treat this as ending
            // the statement.
            return;
          }
        }

        // ??? TODO: support piping with |
        do {
          std::string::size_type tokenPos = pos;
          pos = cmdLine.find_first_of(" \t\xc2", pos);
          while ((pos != std::string::npos) && (cmdLine[pos] == '\xc2') &&
                 ((pos + 1) < cmdLine.size()) && (cmdLine[pos + 1] != '\xa0')) {
            // This will cause mildly strange error message if a non-ascii
            // character other than a non-breaking-blank is found but this
            // doesn't seem like a particular problem if we just want to
            // add support for non-breaking-blank as whitespace for now.
            pos = cmdLine.find_first_of(" \t\xc2", pos + 2);
          }
          std::string token(cmdLine, tokenPos, (pos == std::string::npos)
                                                   ? pos
                                                   : (pos - tokenPos));
          tokens.push_back(token);
          pos = cmdLine.find_first_not_of(" \t", pos);
          while ((pos != std::string::npos) && (cmdLine[pos] == '\xc2') &&
                 ((pos + 1) < cmdLine.size()) && (cmdLine[pos + 1] == '\xa0')) {
            pos = cmdLine.find_first_not_of(" \t", pos + 2);
          }
        } while (pos != std::string::npos);
        if (!checkNextLine) {
          return;
        }
      }
      _inputStack.pop();
      if (!_scriptContext.empty()) {
        if (!input.eof()) {
          LineInfo& lineInfo = _scriptContext.back();
          size_t line = lineInfo._line;
          std::string& path = lineInfo._path;
          std::cerr << "Error at line " << std::dec << line << " of script \""
                    << path << "\"\n";
          if (input.fail()) {
            std::cerr << "Failed to read a command line.\n";
          }
        }
        _scriptContext.pop_back();
      }
      return;  // There is no more input, at least in the current script.
    }
  }
  bool IsDone() { return _inputStack.empty(); }

  bool IsInScript() { return _inputStack.size() > 1; }

 private:
  ScriptContext& _scriptContext;
  std::stack<std::istream*> _inputStack;
};

class Output {
 public:
  Output() { _outputStack.push(&std::cout); }
  ~Output() {}
  bool PushTarget(const std::string& outputPath) {
    std::ofstream* output = new std::ofstream();

    output->open(outputPath.c_str());
    if (output->fail()) {
      delete output;
      return false;
    }
    _outputStack.push(output);
    return true;
  }
  void PopTarget() {
    delete _outputStack.top();
    _outputStack.pop();
  }
  std::ostream& GetTopOutputStream() { return *(_outputStack.top()); }

  void width(int width) { _outputStack.top()->width(width); }

  void HexDump(const uint64_t* image, uint64_t numBytes,
               bool showTrailingAscii) {
    int headerWidth = 0;
    if (numBytes > 0x20) {
      headerWidth = 1;
      for (size_t widthLimit = 0x10; numBytes > widthLimit; widthLimit <<= 4) {
        headerWidth++;
      }
    }
    int offset = 0;
    std::ostream& topStream = *_outputStack.top();
    topStream << std::hex;
    for (const uint64_t* limit =
             image + ((numBytes + sizeof(uint64_t) - 1) / sizeof(uint64_t));
         image < limit; image++) {
      if ((offset & 0x1f) == 0 && headerWidth != 0) {
        topStream.width(headerWidth);
        topStream << offset << ": ";
      }
      topStream.width(sizeof(uint64_t) * 2);
      topStream << *image;
      offset += sizeof(uint64_t);
      if (offset & 0x1f) {
        topStream << " ";
      } else {
        if (showTrailingAscii) {
          ShowTrailingAscii(topStream, 3, ((const char*)(image + 1)) - 0x20,
                            0x20);
        }
        topStream << std::endl;
      }
    }
    size_t trailing = offset & 0x1f;
    if (trailing != 0) {
      if (showTrailingAscii) {
        size_t missing =
            (0x20 - trailing) / sizeof(uint64_t) * (2 * sizeof(uint64_t) + 1) +
            2;
        ShowTrailingAscii(topStream, missing, ((const char*)(image)) - trailing,
                          trailing);
      }
      topStream << std::endl;
    }
  }

  void HexDump(const uint32_t* image, uint32_t numBytes,
               bool showTrailingAscii) {
    int headerWidth = 0;
    if (numBytes > 0x20) {
      headerWidth = 1;
      for (size_t widthLimit = 0x10; numBytes > widthLimit; widthLimit <<= 4) {
        headerWidth++;
      }
    }
    int offset = 0;
    std::ostream& topStream = *_outputStack.top();
    topStream << std::hex;
    for (const uint32_t* limit =
             image + ((numBytes + sizeof(uint32_t) - 1) / sizeof(uint32_t));
         image < limit; image++) {
      if ((offset & 0x1f) == 0 && headerWidth != 0) {
        topStream.width(headerWidth);
        topStream << offset << ": ";
      }
      topStream.width(sizeof(uint32_t) * 2);
      topStream << *image;
      offset += sizeof(uint32_t);
      if (offset & 0x1f) {
        topStream << " ";
      } else {
        if (showTrailingAscii) {
          ShowTrailingAscii(topStream, 3, ((const char*)(image + 1)) - 0x20,
                            0x20);
        }
        topStream << std::endl;
      }
    }
    size_t trailing = offset & 0x1f;
    if (trailing != 0) {
      if (showTrailingAscii) {
        size_t missing =
            (0x20 - trailing) / sizeof(uint32_t) * (2 * sizeof(uint32_t) + 1) +
            2;
        ShowTrailingAscii(topStream, missing, ((const char*)(image)) - trailing,
                          trailing);
      }
      topStream << std::endl;
    }
  }

  /*
   * The goal here is not to escape things in some reversable way but only
   * to make it so the output is all printable ascii.
   */
  void ShowEscapedAscii(const char* chars, size_t numBytes) {
    std::ostream& topStream = *_outputStack.top();
    const char* limit = chars + numBytes;
    while (chars < limit) {
      char c = *(chars++);
      if ((c < ' ' || c > '~') && (c != '\t') && (c != '\r') && (c != '\n')) {
        topStream << "\\x";
        topStream << std::hex << (((int)(c >> 4)) & 0xf);
        topStream << std::hex << (((int)(c)) & 0xf);
      } else {
        topStream << c;
      }
    }
  }

 private:
  std::stack<std::ostream*> _outputStack;
  void ShowTrailingAscii(std::ostream& topStream, size_t numBlanks,
                         const char* chars, size_t numBytes) {
    for (size_t i = 0; i < numBlanks; i++) {
      topStream << ' ';
    }
    const char* limit = chars + numBytes;
    while (chars < limit) {
      char c = *(chars++);
      if (c < ' ' || c > '~') {
        c = '.';
      }
      topStream << c;
    }
  }
};

template <typename T>
Output& operator<<(Output& output, T v) {
  output.GetTopOutputStream() << v;
  return output;
}

class Error {
 public:
  Error(const ScriptContext& scriptContext)
      : _scriptContext(scriptContext), _contextWritePending(false) {}
  ~Error() {}
  void SetContextWritePending() { _contextWritePending = true; }
  void FlushPendingErrorContext() {
    if (_contextWritePending) {
      if (!_scriptContext.empty()) {
        ScriptContext::const_reverse_iterator itEnd = _scriptContext.rend();
        ScriptContext::const_reverse_iterator it = _scriptContext.rbegin();
        std::cerr << "Error at line " << std::dec << it->_line << " of "
                  << it->_path;
        for (++it; it != itEnd; ++it) {
          std::cerr << "\n called from line " << it->_line << " of "
                    << it->_path;
        }
        std::cerr << "\n";
      }
      _contextWritePending = false;
    }
  }

 private:
  const ScriptContext& _scriptContext;
  bool _contextWritePending;
};

// TODO: figure out why this doesn't work with std::endl (but it does work
// with std::dec and std::hex and various values.
// TODO: explore whether it is better for CommandCallback to have fixed notion
//      of _error and output.
template <typename T>
Error& operator<<(Error& error, T v) {
  error.FlushPendingErrorContext();
  std::cerr << v;
  return error;
}

class Context {
 public:
  Context(Input& input, Output& output, Error& error,
          const std::string& redirectPrefix)
      : _input(input),
        _output(output),
        _error(error),
        _redirectPrefix(redirectPrefix),
        _hasIllFormedSwitch(false) {
    _input.GetTokens(_tokens);
    _error.SetContextWritePending();
    std::string switchName;
    size_t argNum = 0;
    for (std::vector<std::string>::const_iterator it = _tokens.begin();
         it != _tokens.end(); ++it) {
      const std::string& token = *it;
      if (token.find('/') == 0) {
        if (!switchName.empty()) {
          /*
           * For now all switches are expected to take an
           * argument.  If at some point this needs to be changed
           * we can add some way to declare switches that don't take
           * arguments.
           */
          _error << "Expected argument for switch " << switchName << "\n";
          _hasIllFormedSwitch = true;
        } else if (argNum == 0) {
          _error << "No switches are allowed before the command name.\n";
          _hasIllFormedSwitch = true;
        }
        switchName = token.substr(1);
        if (switchName.empty()) {
          _error << "An unexpected empty switch name was found.\n";
          _hasIllFormedSwitch = true;
        }
      } else {
        if (switchName.empty()) {
          _positionalArguments.push_back(token);
        } else {
          _switchedArguments[switchName].push_back(token);
          switchName = "";
        }
      }
      argNum++;
    }
    if (!switchName.empty()) {
      /*
       * For now all switches are expected to take an
       * argument.  If at some point this needs to be changed
       * we can add some way to declare switches that don't take
       * arguments.
       */
      _error << "Expected argument for switch " << switchName << "\n";
      _hasIllFormedSwitch = true;
    }
  }

  ~Context() {
    if (!_redirectPath.empty()) {
      _output.PopTarget();
      _output << "Wrote results to " << _redirectPath << "\n";
    }
  }

  bool SetRedirectPathBySuffix() {
    const std::string& suffix = Argument("redirectSuffix", 0);
    if (!suffix.empty()) {
      _redirectPath.append(".");
      _redirectPath.append(suffix);
      return true;
    }
    return false;
  }

  void SetRedirectPathByArguments() {
    for (size_t i = 0; i < _positionalArguments.size(); i++) {
      _redirectPath.append((i == 0) ? "." : "_");
      _redirectPath.append(_positionalArguments[i]);
    }

    for (std::map<std::string, std::vector<std::string> >::const_iterator it =
             _switchedArguments.begin();
         it != _switchedArguments.end(); ++it) {
      _redirectPath.append("::");
      _redirectPath.append(it->first);
      for (std::vector<std::string>::const_iterator itVector =
               it->second.begin();
           itVector != it->second.end(); ++itVector) {
        _redirectPath.append(":");
        _redirectPath.append(*itVector);
      }
    }
  }

  void StartRedirect() {
    if (_redirectPath.empty()) {
      _redirectPath = _redirectPrefix;
      if (!SetRedirectPathBySuffix()) {
        SetRedirectPathByArguments();
      }

      if (_redirectPath.size() > 255) {
        /*
         * Paths that are too long cause an error in the attempt to open them.
         * This is typically exposed using large numbers of switches, as might
         * happen with use of the /extend switch.  For now, just handle this
         * by truncation.
         */
        _redirectPath.resize(255);
      }

      if (!_output.PushTarget(_redirectPath)) {
        _error << "Failed to open " << _redirectPath << " for writing.\n";
        char* openFailCause = strerror(errno);
        if (openFailCause) {
          std::cerr << openFailCause << "\n";
        }
        _redirectPath.clear();
      }
    }
  }

  size_t GetNumTokens() const { return _tokens.size(); }
  const std::string& TokenAt(size_t tokenIndex) const {
    if (tokenIndex < _tokens.size()) {
      return _tokens[tokenIndex];
    } else {
      return emptyToken;
    }
  }

  bool ParseTokenAt(size_t tokenIndex, uint64_t& value) {
    if (tokenIndex < _tokens.size()) {
      std::istringstream is(_tokens[tokenIndex]);
      uint64_t v;
      is >> std::hex >> v;
      if (!is.fail() && is.eof()) {
        value = v;
        return true;
      }
    }
    return false;
  }

  bool ParseTokenAt(size_t tokenIndex, uint32_t& value) {
    if (tokenIndex < _tokens.size()) {
      std::istringstream is(_tokens[tokenIndex]);
      uint32_t v;
      is >> std::hex >> v;
      if (!is.fail() && is.eof()) {
        value = v;
        return true;
      }
    }
    return false;
  }

  size_t GetNumPositionals() const { return _positionalArguments.size(); }
  const std::string& Positional(size_t index) const {
    if (index < _positionalArguments.size()) {
      return _positionalArguments[index];
    } else {
      return emptyToken;
    }
  }

  bool ParsePositional(size_t index, uint64_t& value) {
    if (index < _positionalArguments.size()) {
      std::istringstream is(_positionalArguments[index]);
      uint64_t v;
      is >> std::hex >> v;
      if (!is.fail() && is.eof()) {
        value = v;
        return true;
      }
    }
    return false;
  }

  bool ParsePositional(size_t index, uint32_t& value) {
    if (index < _positionalArguments.size()) {
      std::istringstream is(_positionalArguments[index]);
      uint32_t v;
      is >> std::hex >> v;
      if (!is.fail() && is.eof()) {
        value = v;
        return true;
      }
    }
    return false;
  }

  size_t GetNumArguments(const std::string& switchName) const {
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        _switchedArguments.find(switchName);
    return (it == _switchedArguments.end()) ? 0 : it->second.size();
  }

  const std::string& Argument(const std::string& switchName,
                              size_t index) const {
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        _switchedArguments.find(switchName);
    return (it != _switchedArguments.end() && index < it->second.size())
               ? it->second[index]
               : emptyToken;
  }

  bool ParseArgument(const std::string& switchName, size_t index,
                     uint64_t& value) {
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        _switchedArguments.find(switchName);
    if (it != _switchedArguments.end() && index < it->second.size()) {
      std::istringstream is(it->second[index]);
      uint64_t v;
      is >> std::hex >> v;
      if (!is.fail() && is.eof()) {
        value = v;
        return true;
      }
      _error << "Invalid argument to /" << switchName << ": \""
             << it->second[index] << "\"\n";
    }
    return false;
  }

  bool ParseArgument(const std::string& switchName, size_t index,
                     uint32_t& value) {
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        _switchedArguments.find(switchName);
    if (it != _switchedArguments.end() && index < it->second.size()) {
      std::istringstream is(it->second[index]);
      uint32_t v;
      is >> std::hex >> v;
      if (!is.fail() && is.eof()) {
        value = v;
        return true;
      }
      _error << "Invalid argument to /" << switchName << ": \""
             << it->second[index] << "\"\n";
    }
    return false;
  }

  /*
   * If the specified switch is not present, do nothing, leaving the
   * value as is and return true.  Otherwise, if all occurrences of
   * the switch have the value "true" or "false", set the value
   * accordingly and return true.  In all other cases put a warning
   * to standard error, return false and leave the value untouched.
   */

  bool ParseBooleanSwitch(const std::string& switchName, bool& value) {
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        _switchedArguments.find(switchName);
    if (it == _switchedArguments.end()) {
      return true;
    }
    bool returnValue = value;
    bool returnValueSet = false;
    for (const auto& argument : it->second) {
      if (argument == "true") {
        if (returnValueSet) {
          if (!returnValue) {
            _error << "Conflicting arguments to multiple /" << switchName
                   << " switches.\n";
            return false;
          }
        } else {
          returnValueSet = true;
          returnValue = true;
        }
      } else {
        if (argument == "false") {
          if (returnValueSet) {
            if (returnValue) {
              _error << "Conflicting arguments to multiple /" << switchName
                     << " switches.\n";
              return false;
            }
          } else {
            returnValueSet = true;
            returnValue = false;
          }
        } else {
          _error << "Unexpected argument \"" << argument << "\" to /"
                 << switchName << " switch.\n";
          return false;
        }
      }
    }
    value = returnValue;
    return true;
  }

  bool IsRedirected() { return !_redirectPath.empty(); }
  Output& GetOutput() { return _output; }
  Error& GetError() { return _error; }
  bool HasIllFormedSwitch() const { return _hasIllFormedSwitch; }

 private:
  ScriptContext _scriptContext;
  Input& _input;
  Output& _output;
  Error& _error;
  const std::string& _redirectPrefix;
  bool _hasIllFormedSwitch;
  std::vector<std::string> _tokens;
  std::vector<std::string> _positionalArguments;
  std::map<std::string, std::vector<std::string> > _switchedArguments;
  std::vector<std::string> _commandSource;
  const std::string emptyToken;
  std::string _redirectPath;
};

class Command {
 public:
  Command() {}
  virtual void Run(Context& context) = 0;
  virtual void ShowHelpMessage(Context& context) = 0;
  const virtual std::string& GetName() const = 0;
  virtual void GetSecondTokenCompletions(
      const std::
          string& /* prefix - commented out to avoid compiler warnings */,
      std::function<void(
          const std::
              string&)> /* cb - commented out to avoid compiler warnings */)
      const {}

 protected:
  const std::string _name;
};

typedef std::function<size_t(Context&,  // command context
                             bool)>     // check only
    CommandCallback;

class Runner {
 public:
  Runner(const std::string& redirectPrefix)
      : _redirectPrefix(redirectPrefix),
        _redirect(false),
        _input(_scriptContext),
        _error(_scriptContext),
        _preCommandCallback(nullptr) {}

  void CompletionHook(char const* pref,
                      int /* ctx - commented out to avoid compiler warnings */,
                      replxx_completions* lc) {
    std::string prefix(pref);
    const auto startPos = prefix.find_first_not_of(" \t");
    prefix =
        startPos == std::string::npos ? std::string{} : prefix.substr(startPos);

    const auto spacePos = prefix.find_first_of(" \t");
    const auto subCmdPos = prefix.find_first_not_of(" \t", spacePos);
    for (const auto& c : _commands) {
      const std::string& cname = c.first;
      if (!cname.compare(0, prefix.size(), prefix)) {
        replxx_add_completion(lc, cname.c_str());
        continue;
      }
      if (spacePos == std::string::npos) {
        continue;
      }
      if (prefix.compare(0, spacePos, cname)) {
        continue;
      }

      std::string scPrefix = subCmdPos == std::string::npos
                                 ? std::string{}
                                 : prefix.substr(subCmdPos);

      c.second->GetSecondTokenCompletions(
          scPrefix, [lc](const std::string& completion) {
            replxx_add_completion(lc, completion.c_str());
          });
    }
  }

  void AddCommand(const std::string& commandName,
                  CommandCallback commandCallback) {
    _commandCallbacks[commandName].push_back(commandCallback);
  }

  void AddCommand(Command& command) {
    if (!_commands.insert(std::make_pair(command.GetName(), &command)).second) {
      std::cerr << "Warning: Attempted to declare " << command.GetName()
                << " multiple times.\n";
    }
  }

  Command* FindCommand(const std::string name) {
    std::map<std::string, Command*>::iterator it = _commands.find(name);
    if (it == _commands.end()) {
      return (Command*)0;
    } else {
      return it->second;
    }
  }

  void ShowHelpMessage() {
    _output << "Supported commands are:\nhelp\nredirect\nsource\n";
    for (std::map<std::string, Command*>::iterator it = _commands.begin();
         it != _commands.end(); ++it) {
      _output << it->first << "\n";
    }
    _output << "Use \"help <command-name>\" for help on a specific"
               " command.\n";
  }

  void HandleHelpCommand(Context& context) {
    size_t numTokens = context.GetNumTokens();
    if (numTokens == 1) {
      ShowHelpMessage();
    } else {
      const std::string& topic = context.TokenAt(1);
      if (topic == "redirect") {
        _output << "Use \"redirect on\" to enable redirection"
                   " of output to separate files per command.\n";
        _output << "Use \"redirect off\" to disable redirection"
                   " of output to separate files per\ncommand.\n";
      } else if (topic == "source") {
        _output << "Use \"source <path>\" to run commands"
                   " from the specified file.\n";
      } else if (topic == "help") {
        _output << "Use \"help <command-name>\" for help on"
                   " the specified command.\n";
        _output << "Use \"help\" with no arguments to see"
                   " the following:\n";
        ShowHelpMessage();
      } else {
        std::map<std::string, Command*>::iterator itCommands =
            _commands.find(topic);
        if (itCommands == _commands.end()) {
          _output << "\"" << topic << "\" is not a valid command name.\n";
          ShowHelpMessage();
        } else {
          itCommands->second->ShowHelpMessage(context);
        }
      }
    }
  }

  void HandleRedirectCommand(Context& context) {
    size_t numTokens = context.GetNumTokens();
    // Request for a token after the last one just returns the
    // empty std::string;
    const std::string& argument = context.TokenAt(1);
    if (numTokens != 2 || !(argument == "on" || argument == "off")) {
      _error << "usage:  redirect on|off\n";
    } else {
      _redirect = (argument == "on");
    }
  }

  void HandleSourceCommand(Context& context) {
    size_t numTokens = context.GetNumTokens();
    if (numTokens != 2) {
      _error << "usage:  source <chap-command-file-path>\n";
    } else {
      _input.StartScript(context.TokenAt(1));
    }
  }

  void SetPreCommandCallback(std::function<void()> callback) {
    _preCommandCallback = callback;
  }

  void RunCommands() {
    replxx_install_window_change_handler();
    replxx_set_completion_callback(
        [](char const* prefix, int ctx, replxx_completions* lc, void* ud) {
          static_cast<Runner*>(ud)->CompletionHook(prefix, ctx, lc);
        },
        this);
    while (true) {
      try {
        Context context(_input, _output, _error, _redirectPrefix);
        bool hasIllFormedSwitch = context.HasIllFormedSwitch();
        if (context.HasIllFormedSwitch()) {
          if (context.TokenAt(0).find('/') == 0) {
            continue;
          }
        }
        std::string command = context.TokenAt(0);
        if (command.empty()) {
          // There are no more commands to execute, but perhaps only in
          // the current script.
          if (_input.IsDone()) {
            // There is no more input at all.  Leave the last prompt on
            // its own line.
            _error << "\n";
            return;
          } else {
            // A script just finished.
            continue;
          }
        }
        size_t numTokens = context.GetNumTokens();
        if (command == "help") {
          HandleHelpCommand(context);
        } else if (command == "redirect") {
          HandleRedirectCommand(context);
        } else if (command == "source") {
          HandleSourceCommand(context);
        } else {
          bool redirectStarted = false;
          std::map<std::string, std::list<CommandCallback> >::iterator it =
              _commandCallbacks.find(command);
          if (it != _commandCallbacks.end()) {
            size_t mostTokensAccepted = 0;
            std::list<CommandCallback>::iterator itBest = it->second.end();
            for (std::list<CommandCallback>::iterator itCheck =
                     it->second.begin();
                 itCheck != it->second.end(); ++itCheck) {
              size_t numTokensAccepted = (*itCheck)(context, true);
              if (numTokensAccepted > mostTokensAccepted) {
                mostTokensAccepted = numTokensAccepted;
                itBest = itCheck;
              }
            }
            if (mostTokensAccepted == 0) {
              _error << "unknown command " << command << "\n";
              _input.TerminateAllScripts();
            } else {
              if (_redirect) {
                /*
                 * Redirect for the duration of the command context.  Note
                 * that we don't bother supporting /redirectSuffix for the
                 * old style command callbacks because they are deprecated
                 * and typically were written before switched arguments were
                 * handled separately, so they generally not work as
                 * currently
                 * written if the switch were supplied.
                 */
                redirectStarted = true;
                context.StartRedirect();
              }
              if (mostTokensAccepted == numTokens || mostTokensAccepted >= 2) {
                (*itBest)(context, false);
                continue;
              }
            }
          }
          Command* c = FindCommand(command);
          if (c == (Command*)(0)) {
            _error << "Command " << command << " is not recognized\n";
            _error << "Type \"help\" to get help.\n";
          } else {
            if ((_redirect || !context.Argument("redirectSuffix", 0).empty()) &&
                !redirectStarted) {
              // Redirect for the duration of the command context.
              redirectStarted = true;
              context.StartRedirect();
            }
            if (!hasIllFormedSwitch) {
              if (_preCommandCallback != nullptr) {
                _preCommandCallback();
              }
              c->Run(context);
            }
          }
        }
      } catch (CommandInterruptedException& e) {
        // TODO: support SIG_INT to interrupt long running commands
        _error << "\nThe command was interrupted.\n";
        _input.TerminateAllScripts();
      }
    }
    replxx_history_free();
  }

  ScriptContext _scriptContext;
  const std::string _redirectPrefix;
  bool _redirect;
  Input _input;
  Output _output;
  Error _error;
  std::map<std::string, std::list<CommandCallback> > _commandCallbacks;
  std::map<std::string, Command*> _commands;
  std::function<void()> _preCommandCallback;
};

}  // namespace Commands
}  // namespace chap
